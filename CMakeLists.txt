cmake_minimum_required(VERSION 3.16)
project(chess_bot LANGUAGES CXX OBJCXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug
        Release
        RelWithDebInfo
        MinSizeRel
    )
    message(STATUS "CMAKE_BUILD_TYPE was not set; defaulting to Release")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chess_engine
    third_party/fathom_tbprobe.cpp
    src/engine/bot.cpp
    src/engine/attacks.cpp
    src/engine/chessboard_fen.cpp
    src/engine/chessboard_hash.cpp
    src/engine/chessboard_moves.cpp
    src/engine/chessboard_rules.cpp
    src/engine/chessboard_state.cpp
    src/engine/minimax.cpp
    src/engine/minimax_eval.cpp
    src/engine/minimax_ordering.cpp
    src/engine/minimax_tt.cpp
    src/engine/online_tablebase.cpp
)

target_include_directories(chess_engine
    PUBLIC include
    PRIVATE third_party/Fathom-upstream/src
)

add_library(chess_bench_support
    src/bench/bot_process.cpp
    src/bench/chess_io.cpp
    src/bench/chess_san.cpp
    src/bench/uci_engine.cpp
)

target_include_directories(chess_bench_support PUBLIC include)
target_link_libraries(chess_bench_support PUBLIC chess_engine)

add_executable(chess_engine_bridge
    src/app/engine_bridge_main.cpp
)

target_link_libraries(chess_engine_bridge PRIVATE chess_engine)

add_executable(prepare_bench_dataset
    src/bench/prepare_bench_dataset.cpp
)
target_link_libraries(prepare_bench_dataset PRIVATE chess_bench_support)

if(APPLE)
    add_executable(chess_app MACOSX_BUNDLE
        src/app/gui_main.mm
        src/ui/gui_ui.mm
    )

    target_include_directories(chess_app PRIVATE include)
    target_link_libraries(chess_app PRIVATE chess_engine "-framework Cocoa")
    target_compile_options(chess_app PRIVATE -fobjc-arc)

    add_executable(chess_bench_app MACOSX_BUNDLE
        src/bench/bench_ui_main.mm
        src/bench/bench_ui.mm
    )

    target_include_directories(chess_bench_app PRIVATE include)
    target_link_libraries(chess_bench_app PRIVATE chess_bench_support "-framework Cocoa")
    target_compile_options(chess_bench_app PRIVATE -fobjc-arc)
endif()

add_executable(test_chessboard
    tests/test_chessboard.cpp
)
target_link_libraries(test_chessboard PRIVATE chess_engine)

add_executable(test_minimax
    tests/test_minimax.cpp
)
target_link_libraries(test_minimax PRIVATE chess_engine)

add_executable(test_chessboard_backend
    tests/test_chessboard_backend.cpp
)
target_link_libraries(test_chessboard_backend PRIVATE chess_engine)

add_executable(test_minimax_regression
    tests/test_minimax_regression.cpp
)
target_link_libraries(test_minimax_regression PRIVATE chess_engine)

add_executable(test_bot_forced_ep
    tests/test_bot_forced_ep.cpp
)
target_link_libraries(test_bot_forced_ep PRIVATE chess_engine)

add_executable(bench_minimax
    tests/bench_minimax.cpp
)
target_link_libraries(bench_minimax PRIVATE chess_engine)

add_executable(bench_time
    src/bench-time/bench.cpp
)
target_link_libraries(bench_time PRIVATE chess_engine)

add_executable(perft_tool
    perft/perft_tool.cpp
)
target_link_libraries(perft_tool PRIVATE chess_engine)
