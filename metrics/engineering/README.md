# Engineering Metrics: Reconstruction Agreement

One CSV per ticker, `<TICKER>_agreement.csv`, written by `qmm.sh run|all` or `qmm.ps1 run|all`. Each file measures how closely the reconstructed order book agrees with LOBSTER's ground-truth orderbook file over the trading day.

## Shape

- One header row, then one row per time bucket, oldest first.
- Buckets are 5 minutes wide and consecutive. The 2012-06-21 sample day (09:30–16:00) gives 78 rows.
- Every metric column is a **sum** over the rows (messages) in that bucket. Buckets can be merged by adding columns, and every ratio is computed afterwards (see below).
- No price columns; every size column is in shares.

## Columns

| Column | Type | Meaning |
|---|---|---|
| `bucket_start_ns` | int | Bucket start, nanoseconds since midnight |
| `bucket_start` | `HH:MM:SS` | Same, as clock time |
| `bucket_width_ns` | int | Bucket width in nanoseconds (300000000000 = 5 min) |
| `rows` | int | Messages (orderbook rows) in the bucket |
| `rows_zero_reconcile` | int | Rows where the model already matched the snapshot, so reconciliation changed nothing |
| `bid_price_match` | int | Rows where our best bid price equalled LOBSTER's, before reconciliation |
| `ask_price_match` | int | Same for the best ask |
| `bid_touch_match` | int | Rows where best bid price **and** size both matched, before reconciliation |
| `ask_touch_match` | int | Same for the best ask |
| `depth_abs_error` | shares | Σ \|model − truth\| over the visible top-10 band, both sides, before reconciliation |
| `depth_true` | shares | True visible depth (top 10, both sides) |
| `bid_touch_genesis` | shares | Genesis (unknown-origin) shares at the best bid, after reconciliation |
| `bid_touch_size` | shares | Total shares at the best bid, after reconciliation |
| `ask_touch_genesis` | shares | Same for the best ask |
| `ask_touch_size` | shares | Same for the best ask |

## Derived Ratios

| Ratio | Formula |
|---|---|
| Pure message-driven rows | `rows_zero_reconcile / rows` |
| Best bid / ask price agreement | `bid_price_match / rows`, `ask_price_match / rows` |
| Best bid / ask price+size agreement | `bid_touch_match / rows`, `ask_touch_match / rows` |
| Depth error | `depth_abs_error / depth_true` |
| Genesis share of touch volume | `bid_touch_genesis / bid_touch_size`, `ask_touch_genesis / ask_touch_size` |

For several buckets, sum the columns first and then divide. Averaging per-bucket ratios weights quiet and busy periods equally.

## How to Read It

- **Before reconciliation** columns compare the model right after a message is applied, before it is synced to that row's snapshot. Every earlier row was already reconciled, so these show the error from a single message, not drift accumulated over the day.
- **After reconciliation** columns describe the book a quoting strategy would see. Genesis share of touch is the fraction of the queue at the best price whose individual orders are unknown.
- Depth error counts volume we hold at prices the snapshot omits inside the visible band, as well as size differences at listed prices. Prices beyond the 10th visible level are not compared.
