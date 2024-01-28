//
// Created by Noah Joubert on 18/01/2024.
//

#ifndef READY_TRADER_GO_2024_REALISED_PROFIT_H
#define READY_TRADER_GO_2024_REALISED_PROFIT_H

#include <deque>
#include "types.h"
#include "logger.h"

/* To track realised profit, we need to match bids with asks as they occur.
 * This needs to be done across books and containers, so we have a detached class for this. */
class TradeMatcher {
private:
    Time* time;
    Logger* logger;

    std::vector<Order> filledOrders; // all orders filled
    std::deque<Order> unmatchedBids, unmatchedAsks; // orders waiting to be matched
    void settleFilledOrders() {
        /* match orders as they are filled to work out realised profit */
        while ((!unmatchedBids.empty()) && (!unmatchedAsks.empty())) {
            Order bid = unmatchedBids.front(), ask = unmatchedAsks.front();
            long filledLots = std::min(bid.size, ask.size);
            bid.size -= filledLots;
            ask.size -= filledLots;

            double realisedProfit = ((double)filledLots) * ((double)ask.price - (double)bid.price);

            // pop the orders
            unmatchedBids.pop_front();
            unmatchedAsks.pop_front();

            // only reinsert if they are not filled. we have to do it like this to update the volumes
            if (bid.size != 0) unmatchedBids.push_front(bid);
            if (ask.size != 0) unmatchedAsks.push_front(ask);
        }
    }
public:
    TradeMatcher(Time *timeIn, Logger *loggerIn): time(timeIn), logger(loggerIn) {

    }
    std::vector<Order> *getFilledOrders() {
        return &filledOrders;
    }
    void push(Order order) {
        filledOrders.emplace_back(order);
        if (order.side == Side::BUY) unmatchedBids.push_back(order);
        else if (order.side == Side::SELL) unmatchedAsks.push_back(order);

        settleFilledOrders();
    }
};

#endif //READY_TRADER_GO_2024_REALISED_PROFIT_H
