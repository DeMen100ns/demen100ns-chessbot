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
    src/engine/attacks.cpp
    src/engine/bot.cpp
    src/engine/chess_io.cpp
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
    third_party/fathom_tbprobe.cpp
)

target_include_directories(chess_engine
    PUBLIC include
    PRIVATE third_party/Fathom-upstream/src
)

add_library(chess_bench_support
    src/bench/bot_process.cpp
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

function(add_engine_test target source)
    add_executable(${target}
        ${source}
    )
    target_link_libraries(${target} PRIVATE chess_engine)
    # The tests use assert(), so keep assertions active even in Release builds.
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-UNDEBUG>
    )
endfunction()

add_engine_test(test_chessboard tests/test_chessboard.cpp)
add_engine_test(test_minimax tests/test_minimax.cpp)
add_engine_test(test_chessboard_backend tests/test_chessboard_backend.cpp)
add_engine_test(test_minimax_regression tests/test_minimax_regression.cpp)
add_engine_test(test_minimax_tt tests/test_minimax_tt.cpp)
add_engine_test(test_bot_forced_ep tests/test_bot_forced_ep.cpp)

add_executable(bench_minimax
    tests/bench_minimax.cpp
)
target_link_libraries(bench_minimax PRIVATE chess_engine)

add_executable(bench_time
    src/bench-time/bench.cpp
)
target_link_libraries(bench_time PRIVATE chess_engine)

add_executable(nnue_evaluate_fens
    nnue/evaluate_fens.cpp
)
target_link_libraries(nnue_evaluate_fens PRIVATE chess_engine)

add_executable(perft_tool
    perft/perft_tool.cpp
)
target_link_libraries(perft_tool PRIVATE chess_engine)
