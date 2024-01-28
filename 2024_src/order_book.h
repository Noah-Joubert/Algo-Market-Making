#include "ready_trader_go/types.h"
#include <iostream>
#include <ostream>
#include "logger.h"

#ifndef CPPREADY_TRADER_GO_ORDER_BOOK_H
#define CPPREADY_TRADER_GO_ORDER_BOOK_H

#include "realised_profit.h"
#include "types.h"


class OrderIDGenerator {
    /* Used to generate our client order ids */
private:
    // Private constructor to prevent external instantiation
    OrderIDGenerator() {}
    unsigned long currClientOrderID = 0;
public:
    // Static member function to get the instance of the Singleton class
    static OrderIDGenerator& getInstance() {
        static OrderIDGenerator instance; // This instance is created only once
        return instance;
    }
    unsigned long getNext() {
        currClientOrderID ++;
        return currClientOrderID;
    }
    unsigned long getCurrent() const {
        return currClientOrderID;
    }
};

/* Books store an individual order book internally. One can have many books if they would like.
 * Each book will be bound to either the Futures, or ETF book container.
 * This allows us to separate and track consider separately the profitability of certain classes of trades. */
class Book {
public:
    /* Current position */
    long exposure = 0;
    long submittedBids = 0, submittedAsks = 0;
    double dummyCash = 0;
    OrderIDGenerator *idGenerator;
    OrderList bids, asks;
    Logger *logger;
    Time *time;
    Instrument instrument;

    /* constructors */
    Book() = default; // don't remove dummy constructor
    Book(Instrument inst, Logger *loggerIn, Time *timeIn, OrderIDGenerator *idGen):
        instrument(inst), logger(loggerIn), time(timeIn), idGenerator(idGen) {};

    std::optional<Order> findOrder(long clientOrderID) {
        /* finds an order of a given ID */
        auto bidsID = bids.find(clientOrderID);
        auto asksID = asks.find(clientOrderID);
        if (bidsID != bids.end()) {
            return bidsID->second;
        } else if (asksID != asks.end())  {
            return asksID->second;
        } else {
            return {};
        }
    }
    std::optional<Order> orderFilled(unsigned long clientOrderID, long price, long fillVolume) {
        /* called when an order has been filled */

        // find the order
        std::optional<Order> orderOptional = findOrder(clientOrderID);
        if (!orderOptional.has_value()) {
            return {};
        }

        // handle it
        Order order = orderOptional.value();
        if (order.side == Side::BUY) {
            order.size -= fillVolume;
            bids[clientOrderID] = order;
            exposure += fillVolume;
            submittedBids -= fillVolume;
            dummyCash -= fillVolume * price;

            if (order.size == 0) bids.erase(clientOrderID);
        } else if (order.side == Side::SELL) {
            order.size -= fillVolume;
            asks[clientOrderID] = order;
            exposure -= fillVolume;
            submittedAsks -= fillVolume;
            dummyCash += fillVolume * price;

            if (order.size == 0) asks.erase(clientOrderID);
        }

        // add to the order queue
        lotsFilled += fillVolume;
        Order dummyOrder = order; // create dummy order containing filled lots
        dummyOrder.size = fillVolume;
        dummyOrder.price = price;
        dummyOrder.time = time->getTime();

        return dummyOrder;
    }
    std::optional<Order> orderClosed(unsigned long clientOrderID) {
        /* called when an order has been closed */

        /* Find which market and side the order is in, and cancel it */
        std::optional<Order> orderOptional = findOrder(clientOrderID);
        if (!orderOptional.has_value()) {
            return {};
        }

        Order order = orderOptional.value();
        if (order.side == Side::BUY) {
            submittedBids -= order.size;

            bids.erase(clientOrderID);
        } else if (order.side == Side::SELL) {
            submittedAsks -= order.size;

            asks.erase(clientOrderID);
        }
        return order;
    }
    std::optional<Order> sendOrder(Instrument inst, Side side, long size, long price, unsigned long id = 0) {
        /* called when an order has been sent */
        ordersSent ++;

        /* Store the order */
        long currClientOrderID = (id==0) ? idGenerator->getNext() : id;

        Order newOrder(currClientOrderID, size, price, side, time->getTime(), inst);
        if (side == ReadyTraderGo::Side::BUY) {
            submittedBids += size;
            bids[currClientOrderID] = newOrder;
        } else if (side == ReadyTraderGo::Side::SELL) {
            submittedAsks += size;
            asks[currClientOrderID] = newOrder;
        }
        return newOrder;
    }
    void cancelOrder(unsigned long clientOrderID) {
        /* called when we cancel an order */

        std::optional<Order> orderOptional = findOrder(clientOrderID);
        if (!orderOptional.has_value()) {
            return;
        } else {
            ordersCancelled ++;
        }
    }

    /* Tracking for metrics */
    long ordersSent = 0, lotsFilled = 0, ordersCancelled = 0;
};

/* There will be two instances of this class instantiated, a Futures Book Container, and an ETF Book Container.
 * Each container stores a collection of Book's. Books are only ever interacted with via their container. */
class BooksContainer {
    /* container for our order books */
private:
    std::map<std::string, Book> books;
    Instrument instrument;
    Logger *logger;
    Time *time;
    OrderIDGenerator *idGenerator;
    TradeMatcher *matchingEngine;

    long submittedBids = 0;
    long submittedAsks = 0;
    long exposure = 0;
    double dummyCash = 0;
public:
    BooksContainer(std::vector<std::string> namesIn, Instrument inst, Logger *loggerIn, Time *timeIn, OrderIDGenerator *idGen, TradeMatcher *matcherIn):
    instrument(inst), logger(loggerIn), time(timeIn), idGenerator(idGen), matchingEngine(matcherIn) {
        for (std::string name: namesIn)
            books[name] = Book(instrument, logger, time, idGenerator);
    };

    /* setters */
    void sendOrder(std::string name, Instrument instrument, Side side, long size, long price) {
        if (books.count(name) == 0) return;
        books[name].sendOrder(instrument, side, size, price);
        if (side == Side::BUY) submittedBids += size;
        else if (side == Side::SELL) submittedAsks += size;
    }
    void cancelOrder(unsigned long clientOrderID) {
        for (auto &pair: books) {
            pair.second.cancelOrder(clientOrderID);
        }
    }
    void orderFilled(unsigned long clientOrderID, long price, long fillVolume) {
        for (auto &pair: books) {
            std::optional<Order> orderOptional = pair.second.orderFilled(clientOrderID, price, fillVolume);
            if (orderOptional.has_value()) {
                Order order = orderOptional.value();
                if (Side::BUY == order.side) {
                    exposure += order.size;
                    submittedBids -= order.size;
                    dummyCash -= (double)order.size * (double)order.price;
                } else if (Side::SELL == order.side) {
                    exposure -= order.size;
                    submittedAsks -= order.size;
                    dummyCash += (double)order.size * (double)order.price;
                }

                // create a dummy order which holds the executed trade and save it
                matchingEngine->push(order);
            }
        }
    }
    void orderClosed(unsigned long clientOrderID) {
        for (auto &pair: books) {
            std::optional<Order> orderOptional = pair.second.orderClosed(clientOrderID);
            if (orderOptional.has_value()) {
                Order order = orderOptional.value();
                if (Side::BUY == order.side) submittedBids -= order.size;
                else if (Side::SELL == order.side) submittedAsks -= order.size;
            }
        }
    }

    /* getters */
    long getSubmittedBids() {
        return submittedBids;
    };
    long getSubmitedAsks() {
        return submittedAsks;
    };
    long getExposure() {
        return exposure;
    }
    double getDummyCash() {
        return dummyCash;
    }
    OrderList getBids() {
        OrderList bids;
        for (auto &pair: books) {
            bids.insert(pair.second.bids.begin(), pair.second.bids.end());
        }
        return bids;
    }
    OrderList getAsks() {
        OrderList asks;
        for (auto &pair: books) {
            asks.insert(pair.second.asks.begin(), pair.second.asks.end());
        }
        return asks;
    }
    std::optional<Order> findOrder(long clientOrderID) {
        for (auto &pair: books) {
            std::optional<Order> orderOptional = pair.second.findOrder(clientOrderID);
            if (orderOptional.has_value()) return orderOptional;
        }
        return {};
    }
    std::optional<Book> getBook(std::string name) {
        if (books.count(name) == 0) return {};
        return books[name];
    }
    std::map<std::string, Book> getBooks() {
        return books;
    };
};

#endif //CPPREADY_TRADER_GO_ORDER_BOOK_H
