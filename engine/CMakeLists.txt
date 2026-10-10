# The engine library: core types, order book, and market-data replay.
# Phase 1 populates these. For now it builds as a static library that
# apps/ link against.

add_library(qmm_engine STATIC
    core/version.cpp
    order_book/order_book.cpp
    market_data/lobster_parser.cpp
    market_data/replay.cpp
    market_data/orderbook_file.cpp
    market_data/validator.cpp
)

target_include_directories(qmm_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# C++20 is required by consumers of this library too.
target_compile_features(qmm_engine PUBLIC cxx_std_20)
