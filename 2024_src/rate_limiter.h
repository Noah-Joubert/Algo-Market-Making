//
// Created by Noah Joubert on 17/01/2024.
//

#ifndef READY_TRADER_GO_2024_RATE_LIMITER_H
#define READY_TRADER_GO_2024_RATE_LIMITER_H

#include <chrono>

/* Used to limit the frequency of messages sent to the exchange */
class MessageFrequencyLimiter {
public:
    bool sendMessage() {
        // returns true if a message can be sent, else false
        double time = (std::chrono::system_clock::now() - startTime).count();
        while ( (messageTimes.size() > 0) &&
                (time - messageTimes.front() > 1) )
            messageTimes.pop_front();

        if (messageTimes.size() < messagesPerSecond) {
            messageTimes.push_back(time);
            return true;
        } else {
            return false;
        }
    }
    void setSpeed(int speed) {
        messagesPerSecond = speed * 50;
    }
private:
    // we measure time starting from the instantiation of this class
    std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
    std::deque<double> messageTimes;
    long messagesPerSecond = 50;
};

#endif //READY_TRADER_GO_2024_RATE_LIMITER_H
