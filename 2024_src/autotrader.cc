#include <array>
#include <boost/asio/io_context.hpp>
#include <ready_trader_go/logging.h>
#include <iostream>
#include <algorithm>
#include "autotrader.h"
using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

/* ######################################################################## */
/* UTILITY METHODS BEGIN */
/* ######################################################################## */
/* Order status and utility */
AutoTrader::AutoTrader(boost::asio::io_context& context) : BaseAutoTrader(context)
{
    // Set the etfStream to be calculated by an inverseVWAP
    inverseVwapEstimator.setStream(&etfPriceHistory);

    // set the speed of the frequency limiter
    frequencyLimiter.setSpeed(4); //todo: remember this exists
}
void AutoTrader::DisconnectHandler()
{
    BaseAutoTrader::DisconnectHandler();
    metrics.outputMetrics();
    debugPrint(); // dump status upon disconnect
    RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}
void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId, const std::string& errorMessage)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "error with order " << clientOrderId << ": " << errorMessage;
    if (clientOrderId != 0) orderClosed(clientOrderId);
}
void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId, unsigned long price, unsigned long volume)
{
    orderFilled(clientOrderId, price, volume);
    RLOG(LG_AT, LogLevel::LL_INFO) << "hedge order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " average price in cents";
}
void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId, unsigned long price, unsigned long volume)
{
    orderFilled(clientOrderId, price, volume);
    RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " cents";

}
void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId, unsigned long fillVolume, unsigned long remainingVolume, signed long fees)
{
    //todo: get fees from here
    if (remainingVolume == 0) orderClosed(clientOrderId);
}

/* Utility functions */
bool AutoTrader::sendOrder(std::string name, Instrument instrument, ReadyTraderGo::Side side, long size, long price) {
    if (!frequencyLimiter.sendMessage()) return false;

    /* Validate the order */
    if ((price > ReadyTraderGo::MAXIMUM_ASK) || (price < ReadyTraderGo::MINIMUM_BID)) {
        RLOG(LG_AT, LogLevel::LL_ERROR) << "Order sent at invalid price " << price;
        return false;
    }
    long marketExposure = instrument == Instrument::ETF ? allEtfBooks.getExposure() : allFutureBooks.getExposure();
    long marketBids = instrument == Instrument::ETF ? allEtfBooks.getSubmittedBids()  : allFutureBooks.getSubmittedBids();
    long marketAsks = instrument == Instrument::ETF ? allEtfBooks.getSubmitedAsks() : allFutureBooks.getSubmitedAsks();
    size = side == Side::BUY
            ? std::min(size, TradingParameters::positionLimit - marketExposure - marketBids)
            : std::min(size, marketExposure - marketAsks + TradingParameters::positionLimit);

    if (size <= 0) {
        RLOG(LG_AT, LogLevel::LL_ERROR) << "Order sent for zero lots " << price;
        return false;
    }


    /* Round the price to the tick size */
    constexpr long tickSize = 100;
    price = ((long) ((price + tickSize / 2) / tickSize)) * tickSize;

    /* Send the order */
    if (instrument == Instrument::ETF) {
        allEtfBooks.sendOrder(name, Instrument::ETF, side, size, price);

        SendInsertOrder(idGen.getCurrent(), side, price, size, ReadyTraderGo::Lifespan::GOOD_FOR_DAY);
    } else {
        allFutureBooks.sendOrder(name, Instrument::FUTURE, side, size, price);

        SendHedgeOrder(idGen.getCurrent(), side, (long) price, size);
    }

    /* Log the order */
    logger.orderSent(time.getTime(), instrument, side, idGen.getCurrent(), size, price);
    RLOG(LG_AT, LogLevel::LL_INFO) << side << " order " << idGen.getCurrent() << " sent at " << price << " for " << size << " lots in " << instrument;

    return true;
}
bool AutoTrader::cancelOrder(unsigned long clientOrderID) {
    if (!frequencyLimiter.sendMessage()) return false;

    /* Send the cancel order to the exchange */
    SendCancelOrder(clientOrderID);

    /* Send the cancel order internally */
    allEtfBooks.cancelOrder(clientOrderID);
    allFutureBooks.cancelOrder(clientOrderID);

    /* Log it */
    logger.orderCancelled(time.getTime(), Instrument::ETF, clientOrderID, Side::BUY);
    RLOG(LG_AT, LogLevel::LL_INFO) << "Order " << clientOrderID << " canceled.";
    return true;
}
void AutoTrader::orderFilled(unsigned long clientOrderID, long price, long fillVolume) {
    // find the order before we fill it
    std::optional<Order> etfOptional = allEtfBooks.findOrder(clientOrderID);
    std::optional<Order> futuresOptional = allFutureBooks.findOrder(clientOrderID);

    // fill it
    allEtfBooks.orderFilled(clientOrderID, price, fillVolume);
    allFutureBooks.orderFilled(clientOrderID, price, fillVolume);

    // find which orderbook its from
    if (!(etfOptional.has_value() || futuresOptional.has_value())) return;
    Order order = etfOptional.has_value() ? etfOptional.value() : futuresOptional.value();

    // log the order
    logger.orderFilled(time.getTime(), order.instrument, order.side, order.clientOrderID, fillVolume, price);

    // hedge if we've taken on ETF exposure
    if (order.instrument == Instrument::FUTURE) return;
    order.price = price;
    order.size = fillVolume;
    hedge();
}
void AutoTrader::orderClosed(unsigned long clientOrderID) {
    allEtfBooks.orderClosed(clientOrderID);
    allFutureBooks.orderClosed(clientOrderID);
}
/* ######################################################################## */
/* UTILITY METHODS END */
/* ######################################################################## */


/* Trading Logic methods */
void AutoTrader::OrderBookMessageHandler(Instrument instrument,
                                         unsigned long sequenceNumberIn,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    /* Advance time */
    if (sequenceNumberIn != currSequenceNumber) {
        time.advanceTime(0.25); // we receive a set of books
        currSequenceNumber = sequenceNumberIn;
        assert(instrument != Instrument::ETF); // we should always get the future books first
    }

    /* Store the order book */
    ExchangeOrderBookData exchangeBook = ExchangeOrderBookData(askPrices, askVolumes, bidPrices, bidVolumes);
    if (instrument == Instrument::ETF) {
        etfExchangeOrderBookData.push(exchangeBook);
    } else {
        futureExchangeOrderBookData.push(exchangeBook);
    }

    /* Calculate the fair value */
    std::optional<long> inverseVWAPMid = inverseVwapEstimator.calculateMid(askPrices, askVolumes, bidPrices, bidVolumes);
    std::optional<long> bookMid = inverseVWAPMid; // use the inverseVWAP as the fair value
    if (!bookMid.has_value()) return;

    /* Store the fair value, and orderbook */
    logger.logPrice(time.getTime(), instrument, bookMid.value());
    logger.logOrderbook(time.getTime(), instrument, askPrices, askVolumes, bidPrices, bidVolumes, bookMid.value());

    /* Now break if this isn't an ETF book */
    if (instrument == Instrument::FUTURE) return;

    /* Make the market */
    long futureMid = futureExchangeOrderBookData.back().getMid(); // quote our prices around the mid of the futures
    makeMarket(futureMid, askPrices, askVolumes, bidPrices, bidVolumes);

    /* Store our networth */
    float networth = allEtfBooks.getDummyCash() + allFutureBooks.getDummyCash() + futureMid * (allEtfBooks.getExposure() + allFutureBooks.getExposure());
    networthHistory.push(networth);
}
void AutoTrader::TradeTicksMessageHandler(Instrument instrument,
                                          unsigned long sequenceNumber,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    /* Log the trade ticks, and use them to evaluate mid calculations */
    logger.logTradeTicks(time.getTime(), instrument, askPrices, askVolumes, bidPrices, bidVolumes);
    midMetrics.onTradeTicks(instrument, askPrices, askVolumes, bidPrices, bidVolumes);
}
std::pair<long, long> AutoTrader::detectStaleOrders(long mid, long bidPrice, long askPrice) {
    /* Cancel stale orders */
    long bidsCancelled = 0, asksCancelled = 0;

    static const long allowedUncompetitiveSlippage = 100;
    static const long minSpread = 50; // half sided

    // check we have a valid price //todo: refine this and check elsewhere
    if (bidPrice != 0) {
        for (auto order_pairs: allEtfBooks.getBids()) {
            Order order = order_pairs.second;

            // cancel if too uncompetitive, or too competitive
            if ((order.price - bidPrice > allowedUncompetitiveSlippage) ||
                (mid - order.price < minSpread)) {
                if (cancelOrder(order.clientOrderID))
                    bidsCancelled++;
            }
        }
    }

    if (askPrice != 0) {
        for (auto order_pairs: allEtfBooks.getAsks()) {
            Order order = order_pairs.second;

            // cancel if too uncompetitive, or too competitive
            if ((askPrice - order.price > allowedUncompetitiveSlippage) ||
                (order.price - mid < minSpread)) {
                if (cancelOrder(order.clientOrderID))
                    asksCancelled++;
            }
        }
    }

    return {bidsCancelled, asksCancelled};
}
void AutoTrader::hedge() {
    // hedge all our exposure
    long totalExposure = allEtfBooks.getExposure() + allFutureBooks.getExposure();
    Side side = totalExposure > 0 ? Side::SELL : Side::BUY;

    // at a spread of hedgeSpread from our last quoted price
    static const long hedgeSpread = 100;
    std::optional<double> lastBid = bidPriceHistory.getBack(), lastAsk = askPriceHistory.getBack();
    if (!(lastBid.has_value() && lastAsk.has_value())) return;
    long hedgePrice = side == Side::BUY ? lastBid.value() + hedgeSpread : lastAsk.value() - hedgeSpread;

    // send the order
    if (time.getTime() > 1)
        sendOrder("Future", Instrument::FUTURE, side, std::abs(totalExposure), hedgePrice);
}
std::pair<long, long> AutoTrader::getOrderPrices(long mid,
                                    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {
    /* Produces a set of bid/ ask prices for us to quote at */

    /* ########################### SECTION 1 ########################### */
    /* Determine a set of prices which are at a sensible spread, and make us lead the orderbook */
    /* Specifically, our price is the closest to being at a certain orderbook priority,
     * such that we lie between a (min_spread, max_spread). */

    static const long minSpread = 150, maxSpread = 500; // note this is one-sided, so total spread would be 2*minSpread

    Interval bidRange = Interval(mid - maxSpread, mid - minSpread);
    Interval askRange = Interval(mid + minSpread, mid + maxSpread);

    // we either want to be at priority defaultMaxPriority, or if we are super exposed, trade at the front of the book
    static const long defaultMaxPriority = 0;
    const static long maxAskPriority = 100;
    const static long maxBidPriority = 100;

    // find the prices such that we get a certain orderbook priority
    long priorityBid = 0, priorityAsk = 0;
    long __totalBids = 0, __totalAsks = 0;
    for (int i = 0; i < TOP_LEVEL_COUNT; i++) {
        __totalBids += bidVolumes[i];
        if ((__totalBids >= maxBidPriority) && (priorityBid == 0)) {
            priorityBid = bidPrices[std::max(0, i - 1)];
        }

        __totalAsks += askVolumes[i];
        if ((__totalAsks >= maxAskPriority) && (priorityAsk == 0)) {
            priorityAsk = askPrices[std::max(0, i - 1)];
        }
    }

    long bidPrice = priorityBid != 0 ? bidRange.getClosestToValue(priorityBid) : bidRange.lower;
    long askPrice = priorityAsk != 0 ? askRange.getClosestToValue(priorityAsk) : askRange.upper;
    /* ###########################    END    ########################### */

    /* ########################### SECTION 2 ########################### */
    /* Here we apply adjustments to this idea price based on indicators */

    /* Check for momentum signal */
    std::optional<Signal> signalOptional = repeatedTradeMomentum.getSignal();
    const static long momentumSlippage = 300;
    if (signalOptional.has_value()) {
        if (signalOptional.value() == up_trend) {
            bidPrice += 100;
            askPrice += momentumSlippage;
        } else if (signalOptional.value() == down_trend) {
            bidPrice -= momentumSlippage;
            askPrice -= 100;
        }
    }
    /* ###########################    END    ########################### */


    // finally log this price
    spreadHistory.push(askPrice - bidPrice);
    bidPriceHistory.push(bidPrice);
    askPriceHistory.push(askPrice);

    return {bidPrice, askPrice};
}
void AutoTrader::makeMarket(long mid,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                            const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    /* Get the prices which we quote at */
    std::pair<long, long> prices = getOrderPrices(mid, askPrices, askVolumes, bidPrices, bidVolumes);
    long bidPrice = prices.first;
    long askPrice = prices.second;

    /* Cancel orders that are stale */
    std::pair<long, long> canceledOrders = detectStaleOrders(mid, bidPrice, askPrice);

    /* Try to trade very little, and very often. */
    static const long lotSize = 50;
    static const long maxSubmittedOrders = 50;

    /* Add canceled orders onto order limit */
    long bidSize = std::min(lotSize, maxSubmittedOrders + canceledOrders.first - allEtfBooks.getSubmittedBids());
    long askSize = std::min(lotSize, maxSubmittedOrders + canceledOrders.second - allEtfBooks.getSubmitedAsks());

    /* Send orders */
    sendOrder("ETF", Instrument::ETF, Side::BUY, bidSize, bidPrice);
    sendOrder("ETF", Instrument::ETF, Side::SELL, askSize, askPrice);
}

