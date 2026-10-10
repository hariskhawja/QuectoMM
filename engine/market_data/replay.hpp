#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "core/types.hpp"
#include "order_book/order_book.hpp"

namespace qmm {

// Custom step function after processing message
using StepCallback = std::function<void(std::size_t index, const Message& msg)>;

// Replays the message stream
void replay(const std::vector<Message>& messages,
            OrderBook& book,
            const StepCallback& on_step = {});

}  // namespace qmm
