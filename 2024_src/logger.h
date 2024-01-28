//
// Created by Noah Joubert on 09/01/2024.
//

#include <ready_trader_go/types.h>
#include <map>

#ifndef CPPREADY_TRADER_GO_LOGGER_H
#define CPPREADY_TRADER_GO_LOGGER_H

using namespace ReadyTraderGo;

class Logger {
    /* This class is used to produce log files for later data analysis */
private:
    bool useLogs;
    const std::string tradesSentLogFile = "custom_log/trades_sent.csv";
    const std::string tradesFilledLogFile = "custom_log/trades_filled.csv";
    const std::string tradesCancelledLogFile = "custom_log/trades_cancelled.csv";
    const std::string signalsLogFile = "custom_log/signals.csv";
    const std::string priceHistoryLogFile = "custom_log/prices.csv";
    const std::string orderBookLogFile = "custom_log/order_book.csv";
    const std::string tradeTicksLogFile = "custom_log/trade_ticks.csv";

    std::string getInstrumentString(Instrument instrument) {
        switch (instrument) {
            case Instrument::ETF: return "ETF";
            case Instrument::FUTURE: return "Future";
            default: return "";
        }

    }
    std::string getSideString(Side side) {
        switch (side) {
            case Side::BUY: return "BUY";
            case Side::SELL: return "SELL";
            default: return "";
        }
    }
public:
    const void orderSent(double time, Instrument instrument, Side side, long clientOrderId, long volume, long price) {
        if (!useLogs) return;
        std::string instrumentString = getInstrumentString(instrument);
        std::string sideString = getSideString(side);

        /* Format: time, id, instrument, side, volume, price */
        std::ofstream myfile;
        myfile.open (tradesSentLogFile, std::ios_base::app);
        myfile << time << "," << clientOrderId << "," << instrumentString << "," << sideString << "," << volume << "," << price << "\n";
        myfile.close();
    }
    void orderFilled(double time, Instrument instrument, Side side, long clientOrderID, long fillVolume, long price) {
        if (!useLogs) return;
        std::string instrumentString = getInstrumentString(instrument);
        std::string sideString = getSideString(side);

        /* Format: time, id, instrument, side, volume, price */
        std::ofstream myfile;
        myfile.open (tradesFilledLogFile, std::ios_base::app);
        myfile << time << "," << clientOrderID << "," << instrumentString << "," << sideString << "," << fillVolume << "," << price << "\n";
        myfile.close();
    }
    void orderCancelled(double time, Instrument instrument, long clientOrderID, Side side) {
        // todo: rewrite this so it only takes the time and id.
        std::string sideString = getSideString(side);
        if (!useLogs) return;

        /* Format: time, id, instrument, side */
        std::ofstream myfile;
        myfile.open (tradesCancelledLogFile, std::ios_base::app);
        myfile << time << "," << clientOrderID << "," << getInstrumentString(instrument) << "\n";
        myfile.close();
    }
    void logSignal(double time, std::string name, std::string sig) {
        if (!useLogs) return;

        /* Format: time, signal name, signal */
        std::ofstream myfile;
        myfile.open (signalsLogFile, std::ios_base::app);
        myfile << time << "," << name << "," << sig << "\n";
        myfile.close();
    }
    void logPrice(double time, Instrument name, double price) {
        if (!useLogs) return;

        /* Format: time, signal name, signal */
        std::ofstream myfile;
        myfile.open (priceHistoryLogFile, std::ios_base::app);
        myfile << time << "," << name << "," << price << "\n";
        myfile.close();
    }
    void logTradeTicks(double time, Instrument instrument,
                       const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askPrices,
                       const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askVolumes,
                       const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidPrices,
                       const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidVolumes) {
        if (!useLogs) return;

        std::string instrumentString = getInstrumentString(instrument);
        std::string str_builder;

        for (int i = 0; i<TOP_LEVEL_COUNT; i++) {
            str_builder += "," + std::to_string(askPrices[i]);
            str_builder += "," + std::to_string(askVolumes[i]);
            str_builder += "," + std::to_string(bidPrices[i]);
            str_builder += "," + std::to_string(bidVolumes[i]);
        }

        /* Format: time, signal name, signal */
        std::ofstream myfile;
        myfile.open (tradeTicksLogFile, std::ios_base::app);
        myfile << time << "," << instrumentString << str_builder << std::endl;
        myfile.close();
    }
    void logOrderbook(double time, Instrument instrument,
                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askPrices,
                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &askVolumes,
                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidPrices,
                      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT> &bidVolumes,
                      const double fair_value) {
        if (!useLogs) return;

        std::string instrumentString = getInstrumentString(instrument);
        std::string str_builder;

        for (int i = 0; i<TOP_LEVEL_COUNT; i++) {
            str_builder += "," + std::to_string(askPrices[i]);
            str_builder += "," + std::to_string(askVolumes[i]);
            str_builder += "," + std::to_string(bidPrices[i]);
            str_builder += "," + std::to_string(bidVolumes[i]);
        }

        unsigned long spread = askPrices[0] - bidPrices[0];

        /* Format: time, signal name, signal */
        std::ofstream myfile;
        myfile.open (orderBookLogFile, std::ios_base::app);
        myfile << time << "," << instrumentString << str_builder << "," << fair_value << "," << spread << "\n";
        myfile.close();
    }
    Logger(bool useLogsIn): useLogs(useLogsIn) {
        if (!useLogs) return;
        // clear the file
        std::ofstream myfile;

        // trades sent
        myfile.open (tradesSentLogFile);
        myfile << "time,id,instrument,side,volume,price\n";
        myfile.close();

        // trades filled
        myfile.open (tradesFilledLogFile);
        myfile << "time,id,instrument,side,volume,price\n";
        myfile.close();

        // trades cancelled
        myfile.open (tradesCancelledLogFile);
        myfile << "time,id,instrument,side\n";
        myfile.close();

        // signals
        myfile.open (signalsLogFile);
        myfile << "time,name,signal\n";
        myfile.close();

        // prices
        myfile.open (priceHistoryLogFile);
        myfile << "time,instrument,mid\n";
        myfile.close();

        // OrderBook
        myfile.open (orderBookLogFile);
        std::string str_builder = "time,instrument";
        for (int i = 0; i<TOP_LEVEL_COUNT; i++)
        {
            str_builder += ",askPrice" + std::to_string(i);
            str_builder += ",askVol" + std::to_string(i);
            str_builder += ",bidPrice" + std::to_string(i);
            str_builder += ",bidVol" + std::to_string(i);
        }
        myfile << str_builder + ",eval,spread" << "\n";
        myfile.close();

        // trade ticks
        myfile.open (tradeTicksLogFile);
        str_builder = "time,instrument";
        for (int i = 0; i<TOP_LEVEL_COUNT; i++)
        {
            str_builder += ",askPrice" + std::to_string(i);
            str_builder += ",askVol" + std::to_string(i);
            str_builder += ",bidPrice" + std::to_string(i);
            str_builder += ",bidVol" + std::to_string(i);
        }
        myfile << str_builder << std::endl;
        myfile.close();
    };
};

#endif //CPPREADY_TRADER_GO_LOGGER_H
