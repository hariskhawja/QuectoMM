#pragma once

#include <cstdint>

namespace qmm {
using Price = std::int64_t;     // price in LOBSTER units (dollars * 10000)
using Size  = std::int64_t;     // number of shares
using OrderId = std::int64_t;   // LOBSTER order reference number
using Timestamp = std::int64_t; // nanoseconds since midnight

enum class EventType : std::int8_t {
    NewLimitOrder       = 1,
    PartialCancel       = 2,  // partial deletion of a resting order
    Delete              = 3,  // total deletion of a resting order
    VisibleExecution    = 4,
    HiddenExecution     = 5,
    TradingHalt         = 7,
};

enum class Side : std::int8_t {
    Buy  =  1,   // bid
    Sell = -1,   // ask
};

struct Order {
    OrderId id;
    Side side;
    Price price;
    Size size;       // remaining shares still live on the book (shrinks over lifetime)
    Timestamp time;  // when it entered the book, FIFO priority
};

// Each row in message dataset
struct Message {
    Timestamp time;
    EventType type;
    OrderId id;
    Size size;  // quantity bought/sold
    Price price;
    Side side;
};
}
