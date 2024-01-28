//
// Created by Noah Joubert on 11/01/2024.
//

#ifndef CPPREADY_TRADER_GO_MIDS_H
#define CPPREADY_TRADER_GO_MIDS_H

#include <array>
#include "data_handling.h"

/* This header is for building estimates of the 'mid-point' or 'fair-value'.
 * New Estimators of a fair value are created by inheriting the AbstractMid class.
 * To evaluate an estimator, we bind it to the MidMetrics class. */

class AbstractMid {
    /* Base class for a mid-estimator to be built off - honestly this needn't exist it just feels nicer knowing that each estimator is related by a super class */
protected:
    MarketStream* estimates;
public:
    void setStream(MarketStream *stream) {
        estimates = stream;
    }
    MarketStream *getStream() { return estimates; }
};



class InverseVWAP: public AbstractMid {
private:
    static const long ticksForOutlier = 1000;
    static std::optional<double> calculateInverseVWAP(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askPricesIn,
                                                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askVolumesIn,
                                                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidPricesIn,
                                                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidVolumesIn) {
        /* This code removes outliers and calculates an inverse VWAP, which I found to be the most
         * effective mid calculation (over regularVWAP, simple/ exponential moving averages, and linear regression) */

        /* Ignore outliers */
        std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> askPrices, askVolumes, bidPrices, bidVolumes;
        for (int i = 0; i < ReadyTraderGo::TOP_LEVEL_COUNT; i ++) {
            askPrices[i] = askPricesIn[i];
            askVolumes[i] = (askPrices[i] - askPrices[0]) > ticksForOutlier ? 0 : askVolumesIn[i];
            bidPrices[i] = bidPricesIn[i];
            bidVolumes[i] = (bidPrices[0] - bidPrices[i]) > ticksForOutlier ? 0 : bidVolumesIn[i];
        }

        /* Do an inverse weighted average of average bid and ask prices */
        long totalBidVolume = std::accumulate(bidVolumes.begin(), bidVolumes.end(), (long)0);
        long totalAskVolume = std::accumulate(askVolumes.begin(), askVolumes.end(), (long)0);
        if ((totalBidVolume == 0) || (totalAskVolume == 0)) return {}; // if there aren't orders on both sides, return an empty optional

        double avgAsk = std::inner_product(askPrices.begin(), askPrices.end(), askVolumes.begin(), (double)0) / totalAskVolume;
        double avgBid = std::inner_product(bidPrices.begin(), bidPrices.end(), bidVolumes.begin(), (double)0) / totalBidVolume;

        double price = (avgBid * totalAskVolume + avgAsk * totalBidVolume) / (totalBidVolume + totalAskVolume);

        /* Round the price to the tick size */
        constexpr long tickSize = 100;
        long mid = ((long) ((price + tickSize / 2) / tickSize)) * tickSize; // round to nearest tick

        /* Return it */
        return mid;
    }
public:
    InverseVWAP() {}
    std::optional<long> calculateMid(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askPricesIn,
                        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askVolumesIn,
                        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidPricesIn,
                        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidVolumesIn) {
        /* Do an inverse weighted average of average bid and ask prices */
        std::optional<double> vwapOptional = calculateInverseVWAP(askPricesIn, askVolumesIn, bidPricesIn, bidVolumesIn);
        if (!vwapOptional.has_value()) return {};

        double mid = vwapOptional.value();

        /* Store and return it */
        estimates->push(mid);
        return mid;
    }
};

class MidMetrics {
    /* Calculates the 'mid-metric' for a set of estimations of mid-value. */
private:
    std::map<std::string, MarketStream*> midEstimations; // store references to the prediction streams of each mid
    std::map<std::string, long long> currScores; // current metric for each trader, not yet divided by totalTrades
    long long totalTrades = 0;
public:
    MidMetrics() {};
    void add(std::string name, MarketStream *estimations) {
        /* add a mid function and associated estimation stream */
        midEstimations[name] = estimations;
        currScores[name] = 0;
    }
    void onTradeTicks(Instrument instrument,
                      const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                      const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                      const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                      const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {
        /* Take the raw trade ticks data and use it to calculate the metric */
        /* The metric is the average absolute distance from the mid at which trades are executed */
        if (instrument != Instrument::ETF) { return; }

        /* For each estimate */
        for (auto pair: midEstimations) {
            std::string name = pair.first;
            MarketStream* stream = pair.second;

            std::optional<float> prevMid = stream->getBack();
            if (!prevMid.has_value()) continue;

            /* Sum the absolute distance from the mid where trades took place */
            for (int i = 0; i < TOP_LEVEL_COUNT; i ++) {
                    totalTrades += askVolumes[i] + bidVolumes[i];
                    if (askPrices[i] != 0) {
                        long ticksFromMid = std::abs(askPrices[i] - prevMid.value());
                        currScores[name] += ticksFromMid * askVolumes[i];
                    }

                    if (bidPrices[i] != 0) {
                        long ticksFromMid = std::abs(prevMid.value() - bidPrices[i]);
                        currScores[name] += ticksFromMid * bidVolumes[i];
                    }
                }
        }
    }
    void printMetrics() {
        std::cout << "Fair value score: " << std::endl;
        for (auto pair: currScores) {
            std::cout << "- " << pair.first << ": " << (double)pair.second / (double)totalTrades << std::endl;
        }
        std::cout << std::endl;
    }
};

#endif //CPPREADY_TRADER_GO_MIDS_H
