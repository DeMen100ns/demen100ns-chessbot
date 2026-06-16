#include "online_tablebase.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "tbprobe.h"

namespace {

namespace fs = std::filesystem;

char piece_to_fen(Piece piece) {
    switch (piece) {
        case W_PAWN: return 'P';
        case W_KNIGHT: return 'N';
        case W_BISHOP: return 'B';
        case W_ROOK: return 'R';
        case W_QUEEN: return 'Q';
        case W_KING: return 'K';
        case B_PAWN: return 'p';
        case B_KNIGHT: return 'n';
        case B_BISHOP: return 'b';
        case B_ROOK: return 'r';
        case B_QUEEN: return 'q';
        case B_KING: return 'k';
        case EMPTY: break;
    }
    return '1';
}

std::string board_to_fen(const ChessBoard& board) {
    std::ostringstream fen;

    for (int rank = 7; rank >= 0; --rank) {
        int empty_count = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = board.piece_at(rank * 8 + file);
            if (piece == EMPTY) {
                ++empty_count;
                continue;
            }

            if (empty_count > 0) {
                fen << empty_count;
                empty_count = 0;
            }
            fen << piece_to_fen(piece);
        }

        if (empty_count > 0) {
            fen << empty_count;
        }
        if (rank > 0) {
            fen << '/';
        }
    }

    fen << ' ' << (board.turn == WHITE ? 'w' : 'b') << ' ';

    std::string castling;
    if (board.white_can_castle_kingside) castling.push_back('K');
    if (board.white_can_castle_queenside) castling.push_back('Q');
    if (board.black_can_castle_kingside) castling.push_back('k');
    if (board.black_can_castle_queenside) castling.push_back('q');
    fen << (castling.empty() ? "-" : castling) << ' ';

    if (board.en_passant_square >= 0 && board.en_passant_square < 64) {
        fen << static_cast<char>('a' + (board.en_passant_square % 8))
            << static_cast<char>('1' + (board.en_passant_square / 8));
    } else {
        fen << '-';
    }

    fen << ' ' << board.halfmove_clock << ' ' << board.turn_number;
    return fen.str();
}

std::string shell_escape(const std::string& value) {
    std::string escaped = "'";
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\"'\"'";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

std::optional<Move> move_from_uci(const std::string& uci) {
    if (uci.size() < 4) {
        return std::nullopt;
    }

    auto to_square = [](char file, char rank) -> int {
        if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
            return -1;
        }
        return (rank - '1') * 8 + (file - 'a');
    };

    const int from = to_square(uci[0], uci[1]);
    const int to = to_square(uci[2], uci[3]);
    if (from < 0 || to < 0) {
        return std::nullopt;
    }

    Piece promotion = EMPTY;
    if (uci.size() >= 5) {
        const bool white_promotion = uci[3] == '8';
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4])))) {
            case 'q': promotion = white_promotion ? W_QUEEN : B_QUEEN; break;
            case 'r': promotion = white_promotion ? W_ROOK : B_ROOK; break;
            case 'b': promotion = white_promotion ? W_BISHOP : B_BISHOP; break;
            case 'n': promotion = white_promotion ? W_KNIGHT : B_KNIGHT; break;
            default: return std::nullopt;
        }
    }

    return Move(from, to, promotion);
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    return value.substr(start);
}

bool looks_like_url(const std::string& value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

bool is_existing_directory(const std::string& path) {
    std::error_code error;
    return !path.empty() && fs::is_directory(fs::path(path), error);
}

bool has_castling_rights(const ChessBoard& board) {
    return board.white_can_castle_kingside ||
           board.white_can_castle_queenside ||
           board.black_can_castle_kingside ||
           board.black_can_castle_queenside;
}

unsigned en_passant_square_for_fathom(const ChessBoard& board) {
    return board.en_passant_square >= 0 ? static_cast<unsigned>(board.en_passant_square) : 0U;
}

Piece promotion_piece_from_fathom(unsigned promotes, Color side_to_move) {
    switch (promotes) {
        case TB_PROMOTES_QUEEN: return side_to_move == WHITE ? W_QUEEN : B_QUEEN;
        case TB_PROMOTES_ROOK: return side_to_move == WHITE ? W_ROOK : B_ROOK;
        case TB_PROMOTES_BISHOP: return side_to_move == WHITE ? W_BISHOP : B_BISHOP;
        case TB_PROMOTES_KNIGHT: return side_to_move == WHITE ? W_KNIGHT : B_KNIGHT;
        case TB_PROMOTES_NONE:
        default: return EMPTY;
    }
}

std::optional<Move> move_from_fathom_result(unsigned result, Color side_to_move) {
    if (result == TB_RESULT_FAILED ||
        result == TB_RESULT_CHECKMATE ||
        result == TB_RESULT_STALEMATE) {
        return std::nullopt;
    }

    const int from = static_cast<int>(TB_GET_FROM(result));
    const int to = static_cast<int>(TB_GET_TO(result));
    if (from < 0 || from >= 64 || to < 0 || to >= 64) {
        return std::nullopt;
    }

    return Move(from, to, promotion_piece_from_fathom(TB_GET_PROMOTES(result), side_to_move));
}

struct FathomState {
    std::mutex mutex;
    std::string initialized_path;
    bool initialized = false;
    unsigned largest = 0;
};

FathomState& fathom_state() {
    static FathomState state;
    return state;
}

bool ensure_fathom_initialized(const std::string& path, unsigned& largest, std::string& debug) {
    FathomState& state = fathom_state();
    std::lock_guard<std::mutex> lock(state.mutex);

    if (!state.initialized || state.initialized_path != path) {
        const bool ok = tb_init(path.c_str());
        state.initialized = ok;
        state.initialized_path = ok ? path : std::string();
        state.largest = ok ? TB_LARGEST : 0;
    }

    largest = state.largest;
    if (!state.initialized) {
        debug = "tb_fathom_init_failed";
        return false;
    }

    debug = "tb_fathom_ready_largest=" + std::to_string(state.largest);
    return true;
}

std::optional<Move> choose_move_with_fathom(const ChessBoard& board,
                                            unsigned largest,
                                            std::string& debug) {
    const unsigned result = tb_probe_root(
        board.color_bitboard(WHITE),
        board.color_bitboard(BLACK),
        board.piece_bitboard(W_KING) | board.piece_bitboard(B_KING),
        board.piece_bitboard(W_QUEEN) | board.piece_bitboard(B_QUEEN),
        board.piece_bitboard(W_ROOK) | board.piece_bitboard(B_ROOK),
        board.piece_bitboard(W_BISHOP) | board.piece_bitboard(B_BISHOP),
        board.piece_bitboard(W_KNIGHT) | board.piece_bitboard(B_KNIGHT),
        board.piece_bitboard(W_PAWN) | board.piece_bitboard(B_PAWN),
        static_cast<unsigned>(board.halfmove_clock),
        0U,
        en_passant_square_for_fathom(board),
        board.turn == WHITE,
        nullptr);

    const auto move = move_from_fathom_result(result, board.turn);
    if (!move.has_value()) {
        debug = "tb_fathom_probe_failed_largest=" + std::to_string(largest);
        return std::nullopt;
    }

    std::string uci = "a1a1";
    uci[0] = static_cast<char>('a' + (move->from % 8));
    uci[1] = static_cast<char>('1' + (move->from / 8));
    uci[2] = static_cast<char>('a' + (move->to % 8));
    uci[3] = static_cast<char>('1' + (move->to / 8));
    if (move->promotion != EMPTY) {
        switch (move->promotion) {
            case W_QUEEN:
            case B_QUEEN: uci.push_back('q'); break;
            case W_ROOK:
            case B_ROOK: uci.push_back('r'); break;
            case W_BISHOP:
            case B_BISHOP: uci.push_back('b'); break;
            case W_KNIGHT:
            case B_KNIGHT: uci.push_back('n'); break;
            default: break;
        }
    }

    debug = "tb_fathom_hit_move=" + uci +
            " wdl=" + std::to_string(TB_GET_WDL(result)) +
            " dtz=" + std::to_string(TB_GET_DTZ(result)) +
            " largest=" + std::to_string(largest);
    return move;
}

}  // namespace

bool OnlineTablebase::configure(const std::string& url, int request_timeout_ms) {
    base_url = url;
    timeout_ms = request_timeout_ms;

    const char* python_env = std::getenv("CHESS_TB_PYTHON");
    const char* helper_env = std::getenv("CHESS_TB_HELPER");
    const char* max_pieces_env = std::getenv("CHESS_TB_MAX_PIECES");

    python_executable = (python_env != nullptr && python_env[0] != '\0')
        ? python_env
        : "python3";
    helper_script_path = (helper_env != nullptr && helper_env[0] != '\0')
        ? helper_env
        : "tools/probe_online_tablebase.py";
    max_pieces = (max_pieces_env != nullptr && max_pieces_env[0] != '\0')
        ? std::max(1, std::atoi(max_pieces_env))
        : 7;

    move_cache.clear();
    native_largest = 0;
    backend = TablebaseBackend::Disabled;

    if (!base_url.empty() && timeout_ms > 0 && is_existing_directory(base_url)) {
        std::string debug;
        if (ensure_fathom_initialized(base_url, native_largest, debug) && native_largest > 0) {
            backend = TablebaseBackend::NativeFathom;
            enabled = true;
            last_probe_debug = debug;
            return true;
        }

        enabled = false;
        last_probe_debug = debug.empty() ? "tb_fathom_unavailable" : debug;
        return false;
    }

    enabled = !base_url.empty() && timeout_ms > 0 &&
              !python_executable.empty() && !helper_script_path.empty();
    backend = enabled ? TablebaseBackend::HelperScript : TablebaseBackend::Disabled;
    last_probe_debug = enabled
        ? (looks_like_url(base_url) ? "tb_helper_ready" : "tb_ready")
        : "tb_disabled";
    return enabled;
}

std::optional<Move> OnlineTablebase::choose_move(const ChessBoard& board) const {
    if (!enabled) {
        last_probe_debug = "tb_disabled";
        return std::nullopt;
    }

    const int piece_count = board.count_pieces();
    if (piece_count > max_pieces) {
        last_probe_debug = "tb_skipped_piece_count=" + std::to_string(piece_count);
        return std::nullopt;
    }

    if (backend == TablebaseBackend::NativeFathom) {
        if (piece_count > static_cast<int>(native_largest)) {
            last_probe_debug = "tb_fathom_skipped_largest=" + std::to_string(native_largest) +
                               "_piece_count=" + std::to_string(piece_count);
            return std::nullopt;
        }

        if (has_castling_rights(board)) {
            last_probe_debug = "tb_fathom_skipped_castling_rights";
            return std::nullopt;
        }
    }

    const std::string fen = board_to_fen(board);
    if (const auto cache_it = move_cache.find(fen); cache_it != move_cache.end()) {
        last_probe_debug = cache_it->second.has_value()
            ? "tb_hit_cache"
            : "tb_miss_cache";
        return cache_it->second;
    }

    if (backend == TablebaseBackend::NativeFathom) {
        auto move = choose_move_with_fathom(board, native_largest, last_probe_debug);
        move_cache[fen] = move;
        return move;
    }

    const std::string command = shell_escape(python_executable) + " " +
                                shell_escape(helper_script_path) + " --url " +
                                shell_escape(base_url) + " --timeout-ms " +
                                std::to_string(timeout_ms) + " --fen " +
                                shell_escape(fen) + " 2>/dev/null";

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        last_probe_debug = "tb_probe_error=popen_failed";
        move_cache[fen] = std::nullopt;
        return std::nullopt;
    }

    std::array<char, 256> buffer{};
    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    const std::string uci = trim(output);
    if (uci.empty() || uci == "none") {
        last_probe_debug = "tb_miss_empty";
        move_cache[fen] = std::nullopt;
        return std::nullopt;
    }

    const auto move = move_from_uci(uci);
    last_probe_debug = move.has_value()
        ? "tb_hit_move=" + uci
        : "tb_probe_error=invalid_uci";
    move_cache[fen] = move;
    return move;
}
