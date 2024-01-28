//
// Created by Noah Joubert on 09/01/2024.
//
#ifndef CPPREADY_TRADER_GO_DATA_HANDLING_H
#define CPPREADY_TRADER_GO_DATA_HANDLING_H

#include "ready_trader_go/types.h"
#include <iostream>
#include <optional>
#include <math.h>
#include "order_book.h"

using namespace ReadyTraderGo;

class MarketStream {
    /* A vector wrapper used to store and query a stream of market data */
public:
    MarketStream() {
        data.reserve(1000 * 4); // as we get orderbook data four times a second, for 1000 seconds
        logData.reserve(1000 * 4);
    }

    void push(double value) {
        /* adds to back of the data */

        /* store log data to make it easier to calculate vol. and % return */
        // we impose some quite strict filters which ignore negative balances, else we'd have issue with log()
        if ((!data.empty()) && (data.back() > 0) && (value > 0)) {
            double x = std::log(value / data.back());
            logData.emplace_back(x);
        } else {
            logData.emplace_back(0);
        }

        data.emplace_back(value);
    }
    std::optional<double> getBack() {
        /* returns last data item */
        if (data.empty()) return {};
        return data.back();
    }
    std::optional<double> getBackNth(int n) {
        /* returns nth from the back */
        if (n > data.size() - 1) return {};
        return data[data.size() - 1 - n];
    }
    std::vector<double> *getData() {
        /* returns all data */
        return &data;
    }
    long getSize() {
        /* returns size of data stream */
        return data.size();
    }

    std::optional<double> getMean(int n) {
        return calculateMean(n, data);
    }
    std::optional<double> getStandardDeviation(int n) {
        return calculateStandardDeviation(n, data);
    }

    std::optional<double> getMeanReturn(int n) {
        return calculateMean(n, logData);
    }
    std::optional<double> getVolatility(int n) {
        /* returns vol ie. standard deviation of log returns */
        return calculateStandardDeviation(n, logData);
    }

    static std::optional<double> regressionBeta(int n, std::vector<double> &v) {
        /* work out the gradient of the line of best fit of the last n data points */
        if (v.size() < n) return {};

        std::vector<double> ys(v.end() - n, v.end());
        std::vector<double> xs(n);
        for (int i = 0; i < n; i ++) xs[i] = i;

        double xDotY = std::inner_product(ys.begin(), ys.end(), xs.begin(), 0.0);
        double sumX = std::accumulate(xs.begin(), xs.end(), 0.0);
        double sumY = std::accumulate(ys.begin(), ys.end(), 0.0);
        double sumXSquare = std::accumulate(xs.begin(), xs.end(), 0.0, [](double acc, double element) {return acc + element * element;});
        double beta = (xDotY - sumX * sumY / n) / (sumXSquare - sumX * sumX / n);

        return beta;
    }
    std::optional<double> getRegressNext(int n) {
        /* uses linear regression to estimate the next value in the stream */
        return regressNext(n, data);
    }
private:
    static std::optional<double> calculateMean(int n, std::vector<double> &v) {
        /* note if n = -1, we perform the operation on all the data */
        if (n == - 1) {
            n = v.size();
        } else {
            n = std::min(n, int(v.size()));
        }
        if (n <= 1) return {};

        return std::accumulate(v.end() - n, v.end(), 0.0) / n;
    }
    static std::optional<double> calculateStandardDeviation(int n, std::vector<double> &v) {
        /* note if n = -1, we perform the operation on all the data */
        if (n == - 1) {
            n = v.size();
        } else {
            n = std::min(n, int(v.size()));
        }

        double total = 0;
        std::optional<double> meanOptional = calculateMean(n, v);
        if (!meanOptional.has_value()) return {};
        double mean = meanOptional.value();

        total = std::reduce(v.end() - n,
                            v.end(), 0.0,
                            [&mean] (double a, double b) -> double { return a + (mean - b) * (mean - b); });

        double sigma = std::sqrt(total / (n - 1));

        return sigma;
    }
    static std::optional<double> regressNext(int n, std::vector<double> &v) {
        /* regress the next data element based on the previous n */
        /* note if n = -1, we perform the operation on all the data */
        if (n == - 1) {
            n = v.size();
        }

        if (v.size() < n) return {};

        /* work out the gradient of the line of best fit */
        std::vector<double> ys(v.end() - n, v.end());
        std::vector<double> xs(n);
        for (int i = 0; i < n; i ++) xs[i] = i;

        std::optional<double> betaOptional = regressionBeta(n, v);
        if (!betaOptional.has_value()) return {};
        double beta = betaOptional.value();
        double xBar = std::accumulate(xs.begin(), xs.end(), (double)0) / (double)n;
        double yBar = std::accumulate(ys.begin(), ys.end(), (double)0) / (double)n;
        double alpha = yBar - beta * xBar;
        return alpha + beta * (double)n;
    }

    std::vector<double> data;
    std::vector<double> logData;
};

class TraderMetrics {
    /* This class calculates and outputs metrics that can be used to evaluate the performance of a trader */
private:
    BooksContainer *etfBooks, *futuresBooks;
    MarketStream *networthHistory;
    MarketStream *mid;
    Time* time;

    TraderMetrics(BooksContainer *etfIn, BooksContainer *futuresIn, MarketStream *networthIn, MarketStream*midIn, Time *timeIn):
        etfBooks(etfIn), futuresBooks(futuresIn), networthHistory(networthIn), mid(midIn), time(timeIn) {}
public:
    static TraderMetrics& getInstance(BooksContainer *etfIn, BooksContainer *futuresIn, MarketStream *networthIn, MarketStream*midIn, Time *timeIn) {
        static TraderMetrics instance(etfIn, futuresIn, networthIn, midIn, timeIn); // This instance is created only once
        return instance;
    }
    void outputMetrics() {
        /* Print the metrics to the command line */
        static int printingDelay = 50;
        if (((int)(time->getTime() * 100)) % (printingDelay * 100) != 0) return;

        std::cout << "\nNew Analysis:\n";
        std::map<std::string, Book> etfsBooksMap = etfBooks->getBooks(), futuresBookMap = futuresBooks->getBooks();
        long totalLotsFilled = 0;
        double totalRealisedProfit = 0;
        long totalOrdersSent = 0;
        long totalOrdersCancelled = 0;
        for (auto list: {etfsBooksMap, futuresBookMap}) {
            for (auto pair: list) {
                Book book = pair.second;


                totalLotsFilled += book.lotsFilled;
                long realisedProfit = book.dummyCash + book.exposure * mid->getBack().value_or(0);
                totalRealisedProfit += realisedProfit;
                totalOrdersSent += book.ordersSent;
                totalOrdersCancelled += book.ordersCancelled;

                // don't use /t. Because python
                std::cout << "------=+ Metrics at " << time->getTime() << " for " << pair.first << " +=------" << std::endl;
                std::cout << "- Trading behaviour: " << std::endl
                    << "    - Lots filled per second = " << book.lotsFilled / time->getTime() << "/s = " << book.lotsFilled << " in total" << std::endl
                    << "    - profit per lot = " << realisedProfit / book.lotsFilled / 100.0 << "£" << std::endl
                    << "    - canceled orders / orders sent = " << 100 * book.ordersCancelled / (book.ordersSent) << "%" << std::endl << std::endl;

            }
        }

        std::cout << "------=+ Overall Trading Behaviour at " << time->getTime() << "+=------" << std::endl
                  << "    - Lots filled per second = " << totalLotsFilled / time->getTime() << "/s = " << totalLotsFilled << " in total" << std::endl
                  << "    - profit per lot = " << totalRealisedProfit / totalLotsFilled / 100.0 << "£" << std::endl
                  << "    - canceled orders / orders sent = " << 100 * totalOrdersCancelled / (totalOrdersSent) << "%" << std::endl << std::endl;

        std::cout << "------=+ Overall P&L +=------" << std::endl
                  << "    - Total return = " << networthHistory->getBack().value_or(0) / 100.0 << "£" << std::endl
                << "    - Standard deviation = " << networthHistory->getStandardDeviation(-1).value_or(0) << "%" <<  std::endl;
    }
};

#endif //CPPREADY_TRADER_GO_DATA_HANDLING_H
