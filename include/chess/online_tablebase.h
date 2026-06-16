#pragma once

#include "chessboard.h"

#include <optional>
#include <string>
#include <unordered_map>

enum class TablebaseBackend {
    Disabled,
    NativeFathom,
    HelperScript,
};

struct OnlineTablebase {
    bool enabled = false;
    int timeout_ms = 200;
    int max_pieces = 7;
    std::string base_url;
    std::string python_executable;
    std::string helper_script_path;
    TablebaseBackend backend = TablebaseBackend::Disabled;
    unsigned native_largest = 0;
    mutable std::string last_probe_debug = "tb_disabled";
    mutable std::unordered_map<std::string, std::optional<Move>> move_cache;

    bool configure(const std::string& url, int request_timeout_ms);
    std::optional<Move> choose_move(const ChessBoard& board) const;
};
