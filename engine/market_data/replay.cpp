#include "market_data/replay.hpp"

namespace qmm {

void replay(const std::vector<Message>& messages,
            OrderBook& book,
            const StepCallback& on_step) {

    for (std::size_t i = 0; i < messages.size(); i++) {
        book.apply(messages[i]);

        if (on_step) on_step(i, messages[i]);
    }
}

}  // namespace qmm
