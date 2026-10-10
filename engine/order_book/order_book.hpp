#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/types.hpp"

namespace qmm {

// All resting orders at a single price, in FIFO (price-time) order
// orders.front() = oldest order (highest priority)
// genesis = aggregate unknown-origin volume, at the front of the queue
// genesis sits ahead of all known orders, not tracked by id
// total_size = cached sum = genesis + sum(sizes in orders)
struct PriceLevel {
    Size genesis = 0;
    Size total_size = 0;
    std::list<Order> orders;
};

// Reconciliation / drift metrics, accumulated by OrderBook::reconcile_level
struct ReconcileStats {
    std::size_t calls = 0;            // reconcile_level invocations (visible levels * rows)
    std::size_t fixes = 0;            // calls where delta != 0 (a correction happened)
    std::size_t delta_positive = 0;   // delta > 0  (model missing volume -> genesis grew)
    std::size_t delta_negative = 0;   // delta < 0  (model stale-high   -> genesis shrank)
    std::size_t delta_zero = 0;       // delta == 0 (model is correct)
    std::size_t levels_created = 0;   // reconciliations that created a brand-new level
    Size genesis_added = 0;           // total volume added to genesis (sum of positive deltas)
    Size genesis_removed = 0;         // total volume removed from genesis (abs sum, neg deltas)
    std::size_t ghost_events = 0;     // times |-delta| exceeded genesis (dipped into known vol)
    Size ghost_volume = 0;            // total known volume removed by the ghost case
    std::size_t levels_pruned = 0;    // PL removed from model which were removed outside of level 10 window (not tracked by dataset)

    // Visible ghost: level was present last tick
    // Genesis still could not cover the request
    // Cause: level dropped out of scope earlier
    // Orders deleted and replaced while hidden
    // Zombie orders now inflate the L2 aggregate
    std::size_t ghost_events_visible = 0;
    Size ghost_volume_visible = 0;

    // Unknown-id size exceeded genesis at that level
    // Volume we fall short by (taken from known orders)
    std::size_t genesis_shortfall_events = 0;
    Size genesis_shortfall_volume = 0;

    // Messages for an id an earlier ghost removal erased
    // Proof the ghost path killed a still-live order
    std::size_t msgs_for_ghosted_ids = 0;
};

// Snapshot of one price level, with price and total size
struct LevelView {
    Price price;
    Size size;
};

// Order-Level (L3) Limit Order Book
// Price --> PriceLevel, sorted by price
// OrderId --> OrderLocation, where OrderLocation is exact location in order book
class OrderBook {
public:
    OrderBook() = default;

    // Called by replay loop on message
    void apply(const Message& m);

    // ---- Queries (const) ----

    // Best (highest) bid / best (lowest) ask price, or nullopt if that side is empty
    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;

    // Best level as (price, size), or nullopt if the side is empty
    [[nodiscard]] std::optional<LevelView> best_bid_level() const;
    [[nodiscard]] std::optional<LevelView> best_ask_level() const;

    // Aggregated resting size at a specific price on a side (0 if no such level)
    [[nodiscard]] Size size_at(Side side, Price price) const;

    // Top n levels on a side, best-first
    // Bids descend in price, asks ascend
    // Shorter than n if fewer levels exist
    [[nodiscard]] std::vector<LevelView> top_levels(Side side, std::size_t n) const;

    // All levels on a side from the best price down to `worst` (inclusive),
    // best-first. nullopt = no limit (every level on the side)
    [[nodiscard]] std::vector<LevelView> levels_through(Side side, std::optional<Price> worst) const;

    // Genesis (unknown-origin) volume at a price, 0 if no such level
    [[nodiscard]] Size genesis_at(Side side, Price price) const;

    // Number of distinct occupied price levels on a side (diagnostics/tests)
    [[nodiscard]] std::size_t level_count(Side side) const;

    // Number of individual resting orders currently tracked (diagnostics/tests)
    [[nodiscard]] std::size_t order_count() const noexcept;

    // ---- Reconciliation ----

    // Force this level's aggregate size to match the L2 snapshot
    // delta = snapshot_size - current total at (side, price)
    // Absorbs delta into the genesis block, creating the level if absent
    // Clamps genesis at zero
    // Residual negative delta (the "ghost" case) comes from known volume
    // Updates reconcile stats
    // Driven by the harness, one snapshot_size per visible level per message
    void reconcile_level(Side side, Price price, Size snapshot_size);

    // Force one whole side to match an L2 snapshot
    // visible = snapshot's real levels, best-first, sentinel pads removed
    // depth = requested snapshot depth (10 for our files)
    // Step 1: reconcile_level() every visible price
    // Step 2: prune in-band levels the snapshot omits (stale -> reconciled to 0)
    // Full snapshot (size == depth): band is top-of-book to worst visible price
    // Partial snapshot: it IS the whole side, so the band is unbounded
    // Levels beyond a full snapshot's worst price are out of view, left untouched
    void reconcile_side(Side side, const std::vector<LevelView>& visible, std::size_t depth);

    // Read-only access to accumulated reconciliation metrics.
    [[nodiscard]] const ReconcileStats& reconcile_stats() const noexcept { return rstats_; }

private:
    // Stores exact location of an order
    struct OrderLocation {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };

    // ---- Per-message-type handlers (dispatched from apply) ----
    void add(const Message& m);             // type 1: new limit order
    void partial_cancel(const Message& m);  // type 2: partial deletion
    void delete_order(const Message& m);    // type 3: total deletion
    void execute(const Message& m);         // type 4 (visible); type 5 is a book no-op

    // ---- Helpers ----
    // The map for a side
    std::map<Price, PriceLevel>& book_for(Side side);
    const std::map<Price, PriceLevel>& book_for(Side side) const;

    // Remove qty of unknown-origin volume from genesis at (side, price)
    // For cancel/delete/execute on an id we never saw a type-1 for
    void reduce_genesis(Side side, Price price, Size qty);

    // Side maps
    std::map<Price, PriceLevel> bids_;
    std::map<Price, PriceLevel> asks_;

    // OrderId to exact location
    std::unordered_map<OrderId, OrderLocation> index_;

    // Accumulated reconciliation metrics.
    ReconcileStats rstats_;

    // Prices listed in the previous reconcile_side() call, per side. Used only to
    // classify ghost events as visible-level vs re-entering-level.
    std::vector<Price> prev_visible_bids_;
    std::vector<Price> prev_visible_asks_;
    [[nodiscard]] bool was_visible(Side side, Price price) const;

    // Ids erased by ghost removal (diagnostic only).
    std::unordered_set<OrderId> ghosted_ids_;
    void handle_unknown_id(const Message& m);  // genesis path + diagnostics
};

}
