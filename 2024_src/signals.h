#ifndef READY_TRADER_GO_2024_SIGNALS_H
#define READY_TRADER_GO_2024_SIGNALS_H

#include "ready_trader_go/types.h"
#include <iostream>
#include <vector>
#include <optional>
#include <math.h>
#include "order_book.h"
#include "data_handling.h"
#include "types.h"

/* This library provides a basis for creating new signals */

using namespace ReadyTraderGo;

typedef std::string Signal;
const static inline Signal up_trend = "UPWARDS_TREND";
const static inline Signal down_trend = "DOWNWARDS_TREND";

class AbstractSignal {
    /* Base class for a signal to be built off */
public:
    virtual std::optional<std::string> getSignal() = 0;
};
class RepeatedTradeMomentum : AbstractSignal {
    /* detects momentum by detecting if we trade repeatedly on one side USED) */
private:
    TradeMatcher *matchingEngine;
    Logger *logger;
    Time *time;
public:
    RepeatedTradeMomentum(TradeMatcher *matcherIn, Logger *loggerIn, Time *timerIn): matchingEngine(matcherIn), time(timerIn) {}
    std::optional<Signal> getSignal() {
        /* If we've traded too many times on one side recently, send the signal */
        static const double timePeriod = 1.0; // we look one second backwards
        static const long tradesForSignal = 2;

        std::vector<Order> *filledOrders = matchingEngine->getFilledOrders();
        long bids = 0, asks = 0;
        for (int i = filledOrders->size() - 1; i >= 0; i--) {
            // break when go too far back in time
            Order order = filledOrders->at(i);
            if (time->getTime() - order.time > timePeriod) break;

            if (order.side == Side::BUY) bids ++;
            else if (order.side == Side::SELL) asks ++;
        }

        // if we've been trading on both sides, there is no signal
        if ((bids >= tradesForSignal) && (asks >= tradesForSignal)) {
            return {};
        } else if (bids >= tradesForSignal) {
            return down_trend;
        } else if (asks >= tradesForSignal) {
            return up_trend;
        } else {
            return {};
        }
    }
};
class ShortTermMomentum : AbstractSignal {
     /* detects momentum by taking a regression line of the price mid (UNUSED) */
private:
    constexpr static double betaForSignal = 70;
    const static long historySize = 10;
    MarketStream *data;
    Logger *logger;
public:
    ShortTermMomentum(MarketStream* dataIn, Logger* loggerIn): data(dataIn), logger(loggerIn) {};
    std::optional<Signal> getSignal() {
        /* A momentum signal that sends when the regression slope of the stocks price exceeds a value */
        std::optional<double> betaOptional = data->regressionBeta(historySize, *data->getData());
        if (!betaOptional.has_value()) return {};

        double beta = betaOptional.value();

        logger->logSignal(data->getSize() / 4, "slope", std::to_string(beta));

        /* Is this trend significant? */
        if (beta > betaForSignal) {
            logger->logSignal(data->getSize() / 4, "short term momentum", "0");
            return up_trend;
        } else if (beta < -betaForSignal) {
            logger->logSignal(data->getSize() / 4, "short term momentum", "1");
            return down_trend;
        } else {
            return {};
        }
    }
};

#endif //READY_TRADER_GO_2024_SIGNALS_H
