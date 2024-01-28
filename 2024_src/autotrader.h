#ifndef CPPREADY_TRADER_GO_AUTOTRADER_H
#define CPPREADY_TRADER_GO_AUTOTRADER_H

#include <array>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <ready_trader_go/baseautotrader.h>
#include <ready_trader_go/types.h>
#include <queue>
#include <cmath>
#include <numeric>

#include "mids.h"
#include "logger.h"
#include "data_handling.h"
#include "rate_limiter.h"
#include "signals.h"

using namespace ReadyTraderGo;

struct Interval {
    /* This class stores an interval of values, and has some basic operations */

    double lower, upper; // lower and upper inclusive bounds

    // constructors
    Interval() {
        lower = -1e9;
        upper = 1e9;
    }
    Interval(double l, double u): lower(l), upper(u) {}

    void print() {
        std::cout << "[" << lower << ", " << upper << "]\n";
    }

    // updates the bounds of the interval.
    // if lower > upper, we shift the interval so lower=upper
    void setLowerBound(double bound) {
        lower = std::max(bound, lower);
        upper = std::max(upper, lower);
    }
    void setUpperBound(double bound) {
        upper = std::min(upper, bound);
        lower = std::min(upper, lower);
    }

    double getClosestToValue(double val) {
        // returns the closest number in the interval to the given value
        if (val < lower) return lower;
        if (val > upper) return upper;
        return val;
    }
};

class AutoTrader : public ReadyTraderGo::BaseAutoTrader
{
public:
    explicit AutoTrader(boost::asio::io_context& context);
    void DisconnectHandler() override;
    void ErrorMessageHandler(unsigned long clientOrderId, const std::string& errorMessage) override;
    void HedgeFilledMessageHandler(unsigned long clientOrderId, unsigned long price, unsigned long volume) override;// Called periodically to report the status of an order book.
    void OrderBookMessageHandler(ReadyTraderGo::Instrument instrument,
                                 unsigned long sequenceNumber,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes) override;
    void OrderFilledMessageHandler(unsigned long clientOrderId,
                                   unsigned long price,
                                   unsigned long volume) override;
    void OrderStatusMessageHandler(unsigned long clientOrderId,
                                   unsigned long fillVolume,
                                   unsigned long remainingVolume,
                                   signed long fees) override;
    void TradeTicksMessageHandler(ReadyTraderGo::Instrument instrument,
                                  unsigned long sequenceNumber,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes) override;

private:
    /* Logger */
    bool showMetrics = true;
    bool useLogs = true; // TODO: CRUCIAL: disable if submitting to competition
    Logger logger = Logger(useLogs);

    /* Store market data */
    MarketStream etfPriceHistory = MarketStream(); // store fair values
    MarketStream futuresPriceHistory = MarketStream();
    MarketStream networthHistory = MarketStream(); // store our networth
    MarketStream spreadHistory = MarketStream(); // store our spread
    MarketStream bidPriceHistory = MarketStream(), askPriceHistory = MarketStream(); // store our quoted prices
    std::queue<ExchangeOrderBookData> etfExchangeOrderBookData, futureExchangeOrderBookData; // store exchange data

    /* Time and ID tracking */
    long currSequenceNumber = 0;
    Time time = Time::getInstance();
    OrderIDGenerator idGen = OrderIDGenerator::getInstance();

    /* Order book tracking */
    TradeMatcher matchingEngine = TradeMatcher(&time, &logger);
    std::vector<std::string> etfBookNames = std::vector<std::string>{"ETF"};
    std::vector<std::string> futureBookNames = std::vector<std::string>{"Future"};
    BooksContainer allEtfBooks = BooksContainer(etfBookNames, Instrument::ETF, &logger, &time, &idGen, &matchingEngine);
    BooksContainer allFutureBooks = BooksContainer(futureBookNames, Instrument::FUTURE, &logger, &time, &idGen, &matchingEngine);

    /* Limit message frequency */
    MessageFrequencyLimiter frequencyLimiter;

    /* Track our performance */
    TraderMetrics metrics = TraderMetrics::getInstance(&allEtfBooks, &allFutureBooks, &networthHistory, &etfPriceHistory, &time);

    /* Mid estimates ~ initialised in the autotrader constructor */
    InverseVWAP inverseVwapEstimator = InverseVWAP();

    /* Class to evaluate the estimators */
    MidMetrics midMetrics = MidMetrics();

    /* Signals */
    RepeatedTradeMomentum repeatedTradeMomentum = RepeatedTradeMomentum(&matchingEngine, &logger, &time);

    /* Trading logic */
    void makeMarket(long mid,
                    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes);
    std::pair<long, long> detectStaleOrders(long mid, long bidPrice, long askPrice);
    std::pair<long, long> getOrderPrices(long mid,
                                             const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                             const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                             const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                             const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes);
    void hedge();

    /* Used to send/ cancel an order */
    bool sendOrder(std::string name, Instrument instrument, ReadyTraderGo::Side side, long size, long price);
    bool cancelOrder(unsigned long clientOrderID);

    /* Called when an order is filled or closed */
    void orderFilled(unsigned long clientOrderID, long price, long fillVolume);
    void orderClosed(unsigned long clientOrderID);

    /* Used to print out debugging information */
    void debugPrint();
};

#endif //CPPREADY_TRADER_GO_AUTOTRADER_H
