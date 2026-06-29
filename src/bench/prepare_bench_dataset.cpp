#include "chess/io.h"
#include "bench/chess_san.h"
#include "bench/uci_engine.h"

#include "chess/chessboard.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Options {
    std::string input_path;
    std::string output_path = "data/bench_positions.json";
    std::string stockfish_path;
    std::size_t target_count = 1000;
    std::size_t max_games = 0;
    int eval_depth = 10;
    int min_cp = -30;
    int max_cp = 30;
    std::uint32_t seed = 20260614U;
};

struct GameRecord {
    std::unordered_map<std::string, std::string> tags;
    std::string moves_text;
};

struct BenchPosition {
    std::string id;
    std::string event;
    std::string opening_name;
    std::string game_url;
    std::string result;
    std::string selected_fen;
    std::string selected_move_san;
    std::string selected_move_uci;
    std::vector<std::string> fen_history;
    std::vector<std::string> uci_history;
    int ply_index = 0;
    int move_number = 0;
    int stockfish_eval_cp = 0;
    std::uint64_t position_key = 0;
};

void print_usage() {
    std::cerr << "Usage: prepare_bench_dataset --input <file.pgn> "
                 "[--output data/bench_positions.json] [--stockfish-path /path/to/stockfish] "
                 "[--target-count 1000] [--max-games N] [--eval-depth 10] "
                 "[--min-cp -30] [--max-cp 30] [--seed 20260614]\n";
}

bool parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            options.input_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            options.output_path = argv[++i];
        } else if (arg == "--stockfish-path" && i + 1 < argc) {
            options.stockfish_path = argv[++i];
        } else if (arg == "--target-count" && i + 1 < argc) {
            options.target_count = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--max-games" && i + 1 < argc) {
            options.max_games = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--eval-depth" && i + 1 < argc) {
            options.eval_depth = std::stoi(argv[++i]);
        } else if (arg == "--min-cp" && i + 1 < argc) {
            options.min_cp = std::stoi(argv[++i]);
        } else if (arg == "--max-cp" && i + 1 < argc) {
            options.max_cp = std::stoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            options.seed = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else {
            return false;
        }
    }

    if (options.stockfish_path.empty()) {
        const char* env = std::getenv("CHESS_STOCKFISH_PATH");
        if (env != nullptr && env[0] != '\0') {
            options.stockfish_path = env;
        }
    }

    return !options.input_path.empty() &&
           !options.output_path.empty() &&
           !options.stockfish_path.empty() &&
           options.target_count > 0 &&
           options.eval_depth > 0 &&
           options.min_cp <= options.max_cp;
}

std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    escaped << "\\u"
                            << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(ch))
                            << std::dec << std::setfill(' ');
                } else {
                    escaped << ch;
                }
                break;
        }
    }
    return escaped.str();
}

bool parse_tag_line(const std::string& line, std::string& key, std::string& value) {
    if (line.size() < 5 || line.front() != '[' || line.back() != ']') {
        return false;
    }

    const std::size_t space_pos = line.find(' ');
    if (space_pos == std::string::npos || space_pos <= 1) {
        return false;
    }

    const std::size_t first_quote = line.find('"', space_pos);
    const std::size_t last_quote = line.rfind('"');
    if (first_quote == std::string::npos || last_quote == std::string::npos || last_quote <= first_quote) {
        return false;
    }

    key = line.substr(1, space_pos - 1);
    value = line.substr(first_quote + 1, last_quote - first_quote - 1);
    return true;
}

bool read_next_game(std::istream& input, GameRecord& game) {
    game = GameRecord{};

    std::string line;
    bool saw_tags = false;
    bool saw_moves = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            if (saw_moves) {
                break;
            }
            continue;
        }

        if (!saw_moves && line.front() == '[') {
            std::string key;
            std::string value;
            if (parse_tag_line(line, key, value)) {
                game.tags[key] = value;
                saw_tags = true;
            }
            continue;
        }

        saw_moves = true;
        if (!game.moves_text.empty()) {
            game.moves_text.push_back(' ');
        }
        game.moves_text += line;
    }

    return saw_tags || saw_moves;
}

std::vector<std::string> tokenize_movetext(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    int comment_depth = 0;
    int variation_depth = 0;

    for (char ch : text) {
        if (comment_depth > 0) {
            if (ch == '}') {
                --comment_depth;
            } else if (ch == '{') {
                ++comment_depth;
            }
            continue;
        }
        if (variation_depth > 0) {
            if (ch == '(') {
                ++variation_depth;
            } else if (ch == ')') {
                --variation_depth;
            }
            continue;
        }

        if (ch == '{') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            comment_depth = 1;
            continue;
        }
        if (ch == '(') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            variation_depth = 1;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

bool is_result_token(const std::string& token) {
    return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

std::optional<std::string> normalize_move_token(const std::string& raw_token) {
    if (raw_token.empty()) {
        return std::nullopt;
    }
    if (raw_token[0] == '$') {
        return std::nullopt;
    }

    std::string token = raw_token;
    const std::size_t dot_pos = token.rfind('.');
    if (dot_pos != std::string::npos) {
        token = token.substr(dot_pos + 1);
    }

    if (token.empty() || is_result_token(token)) {
        return std::nullopt;
    }

    return token;
}

std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    std::ostringstream output;
    output << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void write_dataset_json(const Options& options, const std::vector<BenchPosition>& positions) {
    std::ofstream output(options.output_path);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + options.output_path);
    }

    output << "{\n";
    output << "  \"generated_at\": \"" << json_escape(current_timestamp()) << "\",\n";
    output << "  \"input_pgn\": \"" << json_escape(options.input_path) << "\",\n";
    output << "  \"stockfish_path\": \"" << json_escape(options.stockfish_path) << "\",\n";
    output << "  \"stockfish_eval_depth\": " << options.eval_depth << ",\n";
    output << "  \"eval_range_cp\": [" << options.min_cp << ", " << options.max_cp << "],\n";
    output << "  \"positions\": [\n";

    for (std::size_t i = 0; i < positions.size(); ++i) {
        const BenchPosition& position = positions[i];
        output << "    {\n";
        output << "      \"id\": \"" << json_escape(position.id) << "\",\n";
        output << "      \"event\": \"" << json_escape(position.event) << "\",\n";
        output << "      \"opening_name\": \"" << json_escape(position.opening_name) << "\",\n";
        output << "      \"game_url\": \"" << json_escape(position.game_url) << "\",\n";
        output << "      \"result\": \"" << json_escape(position.result) << "\",\n";
        output << "      \"selected_fen\": \"" << json_escape(position.selected_fen) << "\",\n";
        output << "      \"selected_move_san\": \"" << json_escape(position.selected_move_san) << "\",\n";
        output << "      \"selected_move_uci\": \"" << json_escape(position.selected_move_uci) << "\",\n";
        output << "      \"ply_index\": " << position.ply_index << ",\n";
        output << "      \"move_number\": " << position.move_number << ",\n";
        output << "      \"stockfish_eval_cp\": " << position.stockfish_eval_cp << ",\n";
        output << "      \"position_key\": " << position.position_key << ",\n";
        output << "      \"fen_history\": [\n";
        for (std::size_t j = 0; j < position.fen_history.size(); ++j) {
            output << "        \"" << json_escape(position.fen_history[j]) << "\"";
            output << (j + 1 < position.fen_history.size() ? ",\n" : "\n");
        }
        output << "      ],\n";
        output << "      \"uci_history\": [\n";
        for (std::size_t j = 0; j < position.uci_history.size(); ++j) {
            output << "        \"" << json_escape(position.uci_history[j]) << "\"";
            output << (j + 1 < position.uci_history.size() ? ",\n" : "\n");
        }
        output << "      ]\n";
        output << "    }" << (i + 1 < positions.size() ? "," : "") << "\n";
    }

    output << "  ]\n";
    output << "}\n";
}

void reservoir_insert(std::vector<BenchPosition>& positions,
                      std::size_t target_count,
                      std::size_t unique_candidate_index,
                      BenchPosition candidate,
                      std::mt19937& rng) {
    if (positions.size() < target_count) {
        positions.push_back(std::move(candidate));
        return;
    }

    std::uniform_int_distribution<std::size_t> distribution(0, unique_candidate_index - 1);
    const std::size_t slot = distribution(rng);
    if (slot < target_count) {
        positions[slot] = std::move(candidate);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        Options options;
        if (!parse_args(argc, argv, options)) {
            print_usage();
            return 2;
        }

        std::ifstream input(options.input_path);
        if (!input) {
            throw std::runtime_error("Failed to open PGN file: " + options.input_path);
        }

        UciEngine stockfish(options.stockfish_path);
        std::string error;
        if (!stockfish.start(&error)) {
            throw std::runtime_error("Failed to start Stockfish: " + error);
        }
        if (!stockfish.set_option("Threads", "1", &error)) {
            throw std::runtime_error("Failed to configure Stockfish threads: " + error);
        }

        std::mt19937 rng(options.seed);
        std::vector<BenchPosition> positions;
        std::unordered_set<std::uint64_t> seen_position_keys;

        std::size_t games_seen = 0;
        std::size_t parsed_moves = 0;
        std::size_t evaluated_positions = 0;
        std::size_t balanced_positions = 0;
        std::size_t unique_candidates = 0;

        GameRecord game;
        while (read_next_game(input, game)) {
            ++games_seen;
            if (options.max_games > 0 && games_seen > options.max_games) {
                break;
            }

            ChessBoard board;
            board.initialize();

            std::vector<std::string> fen_history;
            std::vector<std::string> uci_history;
            fen_history.push_back(ChessIO::board_to_fen(board));

            std::optional<BenchPosition> game_choice;
            std::size_t game_choice_count = 0;
            bool game_failed = false;

            const std::vector<std::string> raw_tokens = tokenize_movetext(game.moves_text);
            for (const std::string& raw_token : raw_tokens) {
                const auto maybe_token = normalize_move_token(raw_token);
                if (!maybe_token.has_value()) {
                    continue;
                }

                const auto move = ChessSan::move_from_san(board, *maybe_token);
                if (!move.has_value()) {
                    game_failed = true;
                    std::cerr << "warning: failed to parse SAN token '" << *maybe_token
                              << "' in game " << games_seen
                              << " (" << game.tags["Site"] << ")\n";
                    break;
                }

                const std::string move_uci = ChessIO::move_to_uci(*move);
                board = board.make_move(*move);
                fen_history.push_back(ChessIO::board_to_fen(board));
                uci_history.push_back(move_uci);
                ++parsed_moves;

                if (board.generate_moves(board.turn).empty()) {
                    continue;
                }

                ++evaluated_positions;
                const auto search = stockfish.search_fen_depth(fen_history.back(), options.eval_depth, &error);
                if (!search.has_value()) {
                    throw std::runtime_error("Stockfish search failed: " + error);
                }
                if (!search->has_score || search->score_is_mate) {
                    continue;
                }
                if (search->score_cp < options.min_cp || search->score_cp > options.max_cp) {
                    continue;
                }

                ++balanced_positions;
                ++game_choice_count;

                BenchPosition candidate;
                candidate.id = game.tags["Site"] + "#ply" + std::to_string(uci_history.size());
                candidate.event = game.tags["Event"];
                candidate.opening_name = game.tags["Opening"];
                candidate.game_url = game.tags["Site"];
                candidate.result = game.tags["Result"];
                candidate.selected_fen = fen_history.back();
                candidate.selected_move_san = *maybe_token;
                candidate.selected_move_uci = move_uci;
                candidate.fen_history = fen_history;
                candidate.uci_history = uci_history;
                candidate.ply_index = static_cast<int>(uci_history.size());
                candidate.move_number = static_cast<int>((uci_history.size() + 1) / 2);
                candidate.stockfish_eval_cp = search->score_cp;
                candidate.position_key = board.position_key();

                std::uniform_int_distribution<std::size_t> distribution(1, game_choice_count);
                if (!game_choice.has_value() || distribution(rng) == 1) {
                    game_choice = std::move(candidate);
                }
            }

            if (game_failed || !game_choice.has_value()) {
                continue;
            }

            if (!seen_position_keys.insert(game_choice->position_key).second) {
                continue;
            }

            ++unique_candidates;
            reservoir_insert(positions, options.target_count, unique_candidates, std::move(*game_choice), rng);

            if (games_seen % 100 == 0) {
                std::cerr << "progress: games=" << games_seen
                          << " parsed_moves=" << parsed_moves
                          << " evaluated_positions=" << evaluated_positions
                          << " balanced_positions=" << balanced_positions
                          << " unique_candidates=" << unique_candidates
                          << " sampled=" << positions.size() << "\n";
            }
        }

        std::shuffle(positions.begin(), positions.end(), rng);
        write_dataset_json(options, positions);

        std::cerr << "done: games=" << games_seen
                  << " parsed_moves=" << parsed_moves
                  << " evaluated_positions=" << evaluated_positions
                  << " balanced_positions=" << balanced_positions
                  << " unique_candidates=" << unique_candidates
                  << " output_positions=" << positions.size()
                  << " output_file=" << options.output_path << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "prepare_bench_dataset error: " << ex.what() << "\n";
        return 1;
    }
}
