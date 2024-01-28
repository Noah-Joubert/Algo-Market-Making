#ifndef READY_TRADER_GO_2024_TYPES_H
#define READY_TRADER_GO_2024_TYPES_H

struct Order {
    Instrument instrument;
    double time;
    unsigned long clientOrderID;
    unsigned long size;
    unsigned long price;
    Side side;
    Order() {};
    Order(unsigned long clientOrderIDIn, long sizeIn, long priceIn, ReadyTraderGo::Side sideIn, double timeIn, Instrument instrumentIn):
            clientOrderID(clientOrderIDIn), size(sizeIn), price(priceIn), side(sideIn), time(timeIn), instrument(instrumentIn) {};
    void print() const {
        std::cout << "\t" << clientOrderID << ": (Size = " << size << " )" << "(Price = " << price << " )" << "\n";
    };
};
typedef std::map<unsigned long, Order> OrderList;

struct ExchangeOrderBookData{
    // a wrapper around the order-book data we receive from the exchange
    const std::array<unsigned long, TOP_LEVEL_COUNT> askPrices, askVolumes, bidPrices, bidVolumes;
    ExchangeOrderBookData(
            const std::array<unsigned long, TOP_LEVEL_COUNT> askPricesIn,
            const std::array<unsigned long, TOP_LEVEL_COUNT> askVolumesIn,
            const std::array<unsigned long, TOP_LEVEL_COUNT> bidPricesIn,
            const std::array<unsigned long, TOP_LEVEL_COUNT> bidVolumesIn):
            askPrices(askPricesIn), askVolumes(askVolumesIn), bidPrices(bidPricesIn), bidVolumes(bidVolumesIn) {}
    double getMid() {
        return (bidPrices[0] + askPrices[0]) / 2;
    }
};

class Time {
    /* tracks the exchange's time, as we get orderbook data every 0.25s */
private:
    Time() = default;
    double time = 0;
    static constexpr double maxTime = 1000;
public:
    static Time& getInstance() {
        static Time instance; // This instance is created only once
        return instance;
    }
    double getTime() {
        return time;
    }
    double advanceTime(double inc) {
        time += inc;
        return time;
    }
};

#endif //READY_TRADER_GO_2024_TYPES_H
