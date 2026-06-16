#import <Cocoa/Cocoa.h>

#include "bench/bench_app.h"
#include "bench/bot_process.h"
#include "bench/chess_io.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr CGFloat kPieceScale = 0.88;
constexpr CGFloat kPieceYOffsetFactor = 0.01;
constexpr int kMaxBenchPlies = 240;
constexpr int kBenchSearchDepth = 64;

void BenchLog(const std::string& message) {
    FILE* file = std::fopen("/tmp/chess_bench_app.log", "a");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "%s\n", message.c_str());
    std::fclose(file);
}

struct BenchDatasetPosition {
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
};

struct BenchBotSpec {
    std::string folder_path;
    std::string display_name;
    std::string executable_path;
    std::vector<std::string> args;
    std::string working_directory;
};

enum class BenchResult {
    Win,
    Draw,
    Loss,
    Error
};

struct BenchOutcome {
    BenchResult result = BenchResult::Error;
    ChessBoard final_board;
    std::string status_text;
    std::string error_text;
};

bool IsWhitePiece(Piece piece) {
    return piece >= W_PAWN && piece <= W_KING;
}

int FindKingSquare(const ChessBoard& board, Color color) {
    const Piece king = color == WHITE ? W_KING : B_KING;
    for (int square = 0; square < 64; ++square) {
        if (board.has_piece(square, king)) {
            return square;
        }
    }
    return -1;
}

NSString* PieceSymbol(Piece piece) {
    switch (piece) {
        case W_PAWN: return @"\u2659";
        case W_KNIGHT: return @"\u2658";
        case W_BISHOP: return @"\u2657";
        case W_ROOK: return @"\u2656";
        case W_QUEEN: return @"\u2655";
        case W_KING: return @"\u2654";
        case B_PAWN: return @"\u265F";
        case B_KNIGHT: return @"\u265E";
        case B_BISHOP: return @"\u265D";
        case B_ROOK: return @"\u265C";
        case B_QUEEN: return @"\u265B";
        case B_KING: return @"\u265A";
        case EMPTY:
        default:
            return @"";
    }
}

NSString* PieceCacheKey(Piece piece, CGFloat cellSize) {
    return [NSString stringWithFormat:@"%d-%.1f", static_cast<int>(piece), cellSize];
}

NSString* PieceAssetFilename(Piece piece) {
    switch (piece) {
        case W_PAWN: return @"wp.png";
        case W_KNIGHT: return @"wn.png";
        case W_BISHOP: return @"wb.png";
        case W_ROOK: return @"wr.png";
        case W_QUEEN: return @"wq.png";
        case W_KING: return @"wk.png";
        case B_PAWN: return @"bp.png";
        case B_KNIGHT: return @"bn.png";
        case B_BISHOP: return @"bb.png";
        case B_ROOK: return @"br.png";
        case B_QUEEN: return @"bq.png";
        case B_KING: return @"bk.png";
        case EMPTY:
        default:
            return nil;
    }
}

NSArray<NSString*>* AssetSearchRoots() {
    return @[
        [[NSFileManager defaultManager] currentDirectoryPath],
        [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent],
        [[NSBundle mainBundle].executablePath stringByDeletingLastPathComponent] ?: @"",
        [NSBundle mainBundle].resourcePath ?: @""
    ];
}

BOOL PathExists(NSString* path, BOOL expectDirectory) {
    if (path == nil || path.length == 0) {
        return NO;
    }

    BOOL isDirectory = NO;
    const BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDirectory];
    if (!exists) {
        return NO;
    }
    return expectDirectory ? isDirectory : !isDirectory;
}

NSArray<NSString*>* CandidateProjectRoots() {
    NSMutableArray<NSString*>* roots = [NSMutableArray array];

    auto addRootAndParents = ^(NSString* root) {
        if (root == nil || root.length == 0) {
            return;
        }

        NSString* standardized = [root stringByStandardizingPath];
        NSString* current = standardized;
        for (NSInteger depth = 0; depth < 8 && current.length > 1; ++depth) {
            if (![roots containsObject:current]) {
                [roots addObject:current];
            }
            NSString* parent = [current stringByDeletingLastPathComponent];
            if (parent == nil || [parent isEqualToString:current]) {
                break;
            }
            current = parent;
        }
    };

    addRootAndParents([[NSFileManager defaultManager] currentDirectoryPath]);
    addRootAndParents([[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent]);
    addRootAndParents([[NSBundle mainBundle].executablePath stringByDeletingLastPathComponent]);
    addRootAndParents([NSBundle mainBundle].resourcePath);

    return roots;
}

BOOL LooksLikeProjectRoot(NSString* path) {
    if (path == nil || path.length == 0) {
        return NO;
    }

    NSInteger score = 0;
    if (PathExists([path stringByAppendingPathComponent:@"assets"], YES)) {
        score += 1;
    }
    if (PathExists([path stringByAppendingPathComponent:@"bots"], YES)) {
        score += 1;
    }
    if (PathExists([path stringByAppendingPathComponent:@"data"], YES)) {
        score += 1;
    }
    if (PathExists([path stringByAppendingPathComponent:@"build-release"], YES)) {
        score += 1;
    }
    return score >= 2;
}

NSString* ProjectRootPath() {
    static NSString* cached = nil;
    if (cached != nil) {
        return cached;
    }

    for (NSString* candidate in CandidateProjectRoots()) {
        if (LooksLikeProjectRoot(candidate)) {
            cached = candidate;
            return cached;
        }
    }

    cached = [[[NSFileManager defaultManager] currentDirectoryPath] stringByStandardizingPath];
    return cached;
}

NSString* ResolveUserPathNSString(NSString* value, BOOL expectDirectory) {
    if (value == nil) {
        return @"";
    }

    NSString* trimmed = [value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) {
        return @"";
    }

    NSString* expanded = [trimmed stringByExpandingTildeInPath];
    if ([expanded isAbsolutePath]) {
        return [expanded stringByStandardizingPath];
    }

    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSString* projectCandidate = [[ProjectRootPath() stringByAppendingPathComponent:expanded] stringByStandardizingPath];
    [candidates addObject:projectCandidate];

    NSString* cwdCandidate = [[[[NSFileManager defaultManager] currentDirectoryPath]
        stringByAppendingPathComponent:expanded] stringByStandardizingPath];
    if (![candidates containsObject:cwdCandidate]) {
        [candidates addObject:cwdCandidate];
    }

    for (NSString* root in CandidateProjectRoots()) {
        NSString* candidate = [[root stringByAppendingPathComponent:expanded] stringByStandardizingPath];
        if (![candidates containsObject:candidate]) {
            [candidates addObject:candidate];
        }
    }

    for (NSString* candidate in candidates) {
        if (PathExists(candidate, expectDirectory)) {
            return candidate;
        }
    }

    return projectCandidate;
}

std::string ResolveUserPath(const std::string& value, bool expect_directory) {
    NSString* resolved = ResolveUserPathNSString([NSString stringWithUTF8String:value.c_str()], expect_directory);
    return resolved != nil ? std::string([resolved UTF8String]) : std::string();
}

NSString* MakePathDisplayString(NSString* absolutePath) {
    if (absolutePath == nil || absolutePath.length == 0) {
        return @"";
    }

    NSString* projectRoot = ProjectRootPath();
    NSString* standardized = [absolutePath stringByStandardizingPath];
    NSString* prefix = [projectRoot stringByAppendingString:@"/"];
    if ([standardized isEqualToString:projectRoot]) {
        return @".";
    }
    if ([standardized hasPrefix:prefix]) {
        return [standardized substringFromIndex:prefix.length];
    }
    return standardized;
}

NSString* FindPieceAssetPath(Piece piece) {
    NSString* filename = PieceAssetFilename(piece);
    if (filename == nil) {
        return nil;
    }

    NSFileManager* fileManager = [NSFileManager defaultManager];
    for (NSString* root in AssetSearchRoots()) {
        if (root.length == 0) {
            continue;
        }

        NSString* path = [[root stringByAppendingPathComponent:@"assets"]
                               stringByAppendingPathComponent:@"pieces"];
        path = [path stringByAppendingPathComponent:filename];
        if ([fileManager fileExistsAtPath:path]) {
            return path;
        }
    }

    return nil;
}

NSString* FindBoardAssetPath() {
    NSFileManager* fileManager = [NSFileManager defaultManager];
    for (NSString* root in AssetSearchRoots()) {
        if (root.length == 0) {
            continue;
        }

        NSString* path = [[root stringByAppendingPathComponent:@"assets"]
                               stringByAppendingPathComponent:@"boards"];
        path = [path stringByAppendingPathComponent:@"board.png"];
        if ([fileManager fileExistsAtPath:path]) {
            return path;
        }
    }

    return nil;
}

NSImage* DrawPieceImage(Piece piece, CGFloat cellSize) {
    const BOOL isWhite = IsWhitePiece(piece);
    const CGFloat imageSize = cellSize * 0.9;
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(imageSize, imageSize)];
    [image lockFocus];

    [[NSColor clearColor] setFill];
    NSRectFill(NSMakeRect(0, 0, imageSize, imageSize));

    NSString* symbol = PieceSymbol(piece);
    NSFont* font = [NSFont fontWithName:@"Apple Symbols" size:imageSize * 0.86];
    if (font == nil) {
        font = [NSFont fontWithName:@"Times New Roman" size:imageSize * 0.86];
    }
    if (font == nil) {
        font = [NSFont systemFontOfSize:imageSize * 0.86 weight:NSFontWeightBold];
    }

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = NSTextAlignmentCenter;

    NSShadow* shadow = [[NSShadow alloc] init];
    shadow.shadowOffset = NSMakeSize(0.0, -1.0);
    shadow.shadowBlurRadius = 2.5;
    shadow.shadowColor = [[NSColor blackColor] colorWithAlphaComponent:isWhite ? 0.18 : 0.25];

    NSDictionary<NSAttributedStringKey, id>* attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: isWhite
            ? [NSColor colorWithCalibratedRed:0.98 green:0.98 blue:0.99 alpha:1.0]
            : [NSColor colorWithCalibratedRed:0.14 green:0.17 blue:0.21 alpha:1.0],
        NSStrokeColorAttributeName: isWhite
            ? [NSColor colorWithCalibratedRed:0.46 green:0.50 blue:0.56 alpha:0.9]
            : [NSColor colorWithCalibratedRed:0.86 green:0.88 blue:0.92 alpha:0.65],
        NSStrokeWidthAttributeName: @(-3.5),
        NSParagraphStyleAttributeName: paragraph,
        NSShadowAttributeName: shadow
    };

    [symbol drawInRect:NSMakeRect(0, imageSize * 0.01, imageSize, imageSize) withAttributes:attrs];
    [image unlockFocus];
    return image;
}

NSImage* LoadPieceAsset(Piece piece, CGFloat cellSize) {
    NSString* path = FindPieceAssetPath(piece);
    if (path == nil) {
        return nil;
    }

    NSImage* image = [[NSImage alloc] initWithContentsOfFile:path];
    if (image == nil) {
        return nil;
    }

    [image setSize:NSMakeSize(cellSize * kPieceScale, cellSize * kPieceScale)];
    return image;
}

std::string DefaultDatasetPath() {
    const char* env = std::getenv("CHESS_BENCH_DATASET");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }

    NSString* generated = ResolveUserPathNSString(@"data/bench_positions_generated.json", NO);
    if (PathExists(generated, NO)) {
        return "data/bench_positions_generated.json";
    }
    return "data/bench_positions.json";
}

std::string DefaultBotFolderFromEnv(const char* name, const std::string& fallback) {
    const char* env = std::getenv(name);
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return fallback;
}

bool GetJsonString(NSDictionary* object, NSString* key, std::string& value);
std::vector<std::string> GetJsonStringArray(NSDictionary* object, NSString* key);

std::string ResolvePath(const std::string& base, const std::string& value) {
    if (value.empty()) {
        return base;
    }
    if (!value.empty() && value.front() == '/') {
        return value;
    }
    if (base.empty() || base == ".") {
        return value;
    }
    return base + "/" + value;
}

bool LoadBotSpecFromFolder(const std::string& folder_path,
                           BenchBotSpec& spec,
                           std::string& error) {
    if (folder_path.empty()) {
        error = "Bot folder path is empty";
        return false;
    }

    NSString* manifestPath = [NSString stringWithUTF8String:ResolvePath(folder_path, "bot.json").c_str()];
    NSData* data = [NSData dataWithContentsOfFile:manifestPath];
    if (data == nil) {
        error = "Could not read bot manifest at " + ResolvePath(folder_path, "bot.json");
        return false;
    }

    NSError* parseError = nil;
    id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&parseError];
    if (root == nil || ![root isKindOfClass:[NSDictionary class]]) {
        error = parseError != nil
            ? std::string([[parseError localizedDescription] UTF8String])
            : "Bot manifest is not a JSON object";
        return false;
    }

    NSDictionary* dict = (NSDictionary*)root;
    std::string protocol;
    if (!GetJsonString(dict, @"protocol", protocol) || protocol != "chess-bench-v1") {
        error = "Bot manifest protocol must be chess-bench-v1";
        return false;
    }

    std::string entry;
    if (!GetJsonString(dict, @"entry", entry) || entry.empty()) {
        error = "Bot manifest is missing entry";
        return false;
    }

    std::string name;
    if (!GetJsonString(dict, @"name", name) || name.empty()) {
        NSString* folderString = [NSString stringWithUTF8String:folder_path.c_str()];
        name = std::string([[folderString lastPathComponent] UTF8String]);
    }

    std::string cwd;
    GetJsonString(dict, @"cwd", cwd);
    if (cwd.empty()) {
        cwd = ".";
    }

    spec.folder_path = folder_path;
    spec.display_name = name;
    spec.executable_path = ResolvePath(folder_path, entry);
    spec.working_directory = ResolvePath(folder_path, cwd);
    spec.args = GetJsonStringArray(dict, @"args");
    return true;
}

bool GetJsonString(NSDictionary* object, NSString* key, std::string& value) {
    id raw = object[key];
    if (![raw isKindOfClass:[NSString class]]) {
        return false;
    }
    value = std::string([(NSString*)raw UTF8String]);
    return true;
}

int GetJsonInt(NSDictionary* object, NSString* key, int default_value = 0) {
    id raw = object[key];
    if ([raw respondsToSelector:@selector(intValue)]) {
        return [raw intValue];
    }
    return default_value;
}

std::vector<std::string> GetJsonStringArray(NSDictionary* object, NSString* key) {
    std::vector<std::string> values;
    id raw = object[key];
    if (![raw isKindOfClass:[NSArray class]]) {
        return values;
    }

    for (id item in (NSArray*)raw) {
        if ([item isKindOfClass:[NSString class]]) {
            values.emplace_back([(NSString*)item UTF8String]);
        }
    }
    return values;
}

bool LoadDatasetFile(const std::string& path,
                     std::vector<BenchDatasetPosition>& positions,
                     std::string& error) {
    NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
    if (data == nil) {
        error = "Could not read dataset file: " + path;
        return false;
    }

    NSError* parse_error = nil;
    id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&parse_error];
    if (root == nil || ![root isKindOfClass:[NSDictionary class]]) {
        error = parse_error != nil
            ? std::string([[parse_error localizedDescription] UTF8String])
            : "Dataset root is not a JSON object";
        return false;
    }

    id raw_positions = [(NSDictionary*)root objectForKey:@"positions"];
    if (![raw_positions isKindOfClass:[NSArray class]]) {
        error = "Dataset is missing a positions array";
        return false;
    }

    positions.clear();
    for (id item in (NSArray*)raw_positions) {
        if (![item isKindOfClass:[NSDictionary class]]) {
            continue;
        }

        NSDictionary* dict = (NSDictionary*)item;
        BenchDatasetPosition position;
        if (!GetJsonString(dict, @"selected_fen", position.selected_fen)) {
            continue;
        }
        GetJsonString(dict, @"id", position.id);
        GetJsonString(dict, @"event", position.event);
        GetJsonString(dict, @"opening_name", position.opening_name);
        GetJsonString(dict, @"game_url", position.game_url);
        GetJsonString(dict, @"result", position.result);
        GetJsonString(dict, @"selected_move_san", position.selected_move_san);
        GetJsonString(dict, @"selected_move_uci", position.selected_move_uci);
        position.fen_history = GetJsonStringArray(dict, @"fen_history");
        position.uci_history = GetJsonStringArray(dict, @"uci_history");
        position.ply_index = GetJsonInt(dict, @"ply_index", 0);
        position.move_number = GetJsonInt(dict, @"move_number", 0);
        position.stockfish_eval_cp = GetJsonInt(dict, @"stockfish_eval_cp", 0);
        positions.push_back(std::move(position));
    }

    if (positions.empty()) {
        error = "Dataset contains no valid positions";
        return false;
    }

    return true;
}

std::string BuildPositionDetails(const BenchDatasetPosition& position,
                                 const std::string& bot_a_name,
                                 const std::string& bot_b_name,
                                 const std::string& bot_a_color,
                                 const std::string& live_fen,
                                 const std::string& live_status,
                                 int game_index,
                                 int total_games) {
    std::ostringstream details;
    details << "Game " << game_index << "/" << total_games << "\n";
    details << "Bot A: " << bot_a_name << "\n";
    details << "Bot B: " << bot_b_name << "\n";
    details << "Bot A color this game: " << bot_a_color << "\n";
    details << "Opening: " << position.opening_name << "\n";
    details << "Source result: " << position.result << "\n";
    details << "Selected move: " << position.selected_move_san
            << " (" << position.selected_move_uci << ")\n";
    details << "Move number: " << position.move_number
            << " | Ply: " << position.ply_index << "\n";
    details << "Stockfish eval: " << position.stockfish_eval_cp << " cp\n";
    details << "Link: " << position.game_url << "\n";
    details << "Live status: " << live_status << "\n";
    details << "Live FEN: " << live_fen << "\n";
    details << "\nFEN history:\n";
    for (std::size_t i = 0; i < position.fen_history.size(); ++i) {
        details << i << ": " << position.fen_history[i] << "\n";
    }
    return details.str();
}

std::string ExtractBotDebugText(const std::vector<std::string>& info_lines) {
    if (info_lines.empty()) {
        return "Waiting for move...";
    }

    const std::string& line = info_lines.back();
    const std::size_t debug_pos = line.find("\tdebug=");
    const std::size_t depth_pos = line.find("\tcompleted_depth=");

    std::ostringstream text;
    if (depth_pos != std::string::npos) {
        const std::size_t depth_value_start = depth_pos + std::string("\tcompleted_depth=").size();
        const std::size_t depth_value_end = line.find('\t', depth_value_start);
        text << "completed_depth="
             << line.substr(depth_value_start, depth_value_end == std::string::npos
                                                  ? std::string::npos
                                                  : depth_value_end - depth_value_start);
    }

    if (debug_pos != std::string::npos) {
        if (text.tellp() > 0) {
            text << " | ";
        }
        text << line.substr(debug_pos + 1);
    } else if (text.tellp() == 0) {
        text << line;
    }

    return text.str();
}

template <typename Callback>
BenchOutcome PlayBenchMatch(const BenchDatasetPosition& position,
                            BotProcess& bot_a,
                            int bot_a_time_ms,
                            BotProcess& bot_b,
                            int bot_b_time_ms,
                            bool bot_a_on_turn_side,
                            Callback&& on_update) {
    BenchOutcome outcome;
    ChessBoard board(position.selected_fen);
    outcome.final_board = board;

    std::unordered_map<std::uint64_t, int> repetition_count;
    std::vector<std::string> live_fen_history = position.fen_history;
    for (const std::string& fen : position.fen_history) {
        try {
            ++repetition_count[ChessBoard(fen).position_key()];
        } catch (...) {
        }
    }
    if (repetition_count.empty()) {
        ++repetition_count[board.position_key()];
        live_fen_history.push_back(ChessIO::board_to_fen(board));
    }

    const Color bot_a_color = bot_a_on_turn_side ? board.turn : (board.turn == WHITE ? BLACK : WHITE);
    std::string bot_a_debug = "Waiting for move...";
    std::string bot_b_debug = "Waiting for move...";
    on_update(board, "Starting position", bot_a_debug, bot_b_debug);

    for (int ply = 0; ply < kMaxBenchPlies; ++ply) {
        const std::uint64_t current_key = board.position_key();
        if (repetition_count[current_key] >= 3) {
            outcome.result = BenchResult::Draw;
            outcome.status_text = "Draw by repetition";
            outcome.final_board = board;
            return outcome;
        }
        if (board.halfmove_clock >= 100) {
            outcome.result = BenchResult::Draw;
            outcome.status_text = "Draw by 50-move rule";
            outcome.final_board = board;
            return outcome;
        }

        const std::vector<Move> legal_moves = board.generate_moves(board.turn);
        if (legal_moves.empty()) {
            if (board.is_checkmate(board.turn)) {
                const bool bot_a_wins = board.turn != bot_a_color;
                outcome.result = bot_a_wins ? BenchResult::Win : BenchResult::Loss;
                outcome.status_text = bot_a_wins ? "Bot A wins by checkmate" : "Bot B wins by checkmate";
            } else {
                outcome.result = BenchResult::Draw;
                outcome.status_text = "Draw by stalemate";
            }
            outcome.final_board = board;
            return outcome;
        }

        Move chosen_move(0, 0);
        std::string status_note;
        BotProcess* active_bot = board.turn == bot_a_color ? &bot_a : &bot_b;
        const bool is_bot_a_turn = active_bot == &bot_a;
        const int time_ms = is_bot_a_turn ? bot_a_time_ms : bot_b_time_ms;

        std::string error;
        const auto search = active_bot->search_move(kBenchSearchDepth,
                                                    time_ms,
                                                    ChessIO::board_to_fen(board),
                                                    live_fen_history,
                                                    &error);
        if (!search.has_value()) {
            outcome.result = is_bot_a_turn ? BenchResult::Loss : BenchResult::Win;
            outcome.status_text = is_bot_a_turn ? "Bot A failed to return a move" : "Bot B failed to return a move";
            outcome.error_text = error;
            outcome.final_board = board;
            return outcome;
        }
        const auto parsed_move = ChessIO::move_from_uci(search->bestmove_uci);
        if (!parsed_move.has_value() || !board.valid_move(*parsed_move, board.turn)) {
            outcome.result = is_bot_a_turn ? BenchResult::Loss : BenchResult::Win;
            outcome.status_text = is_bot_a_turn ? "Bot A returned an invalid move" : "Bot B returned an invalid move";
            outcome.error_text = search->bestmove_uci;
            outcome.final_board = board;
            return outcome;
        }
        chosen_move = *parsed_move;
        status_note = std::string(is_bot_a_turn ? "Bot A played " : "Bot B played ") + search->bestmove_uci;
        if (!search->info_lines.empty()) {
            BenchLog("bench info: " + search->info_lines.back());
            if (is_bot_a_turn) {
                bot_a_debug = ExtractBotDebugText(search->info_lines);
            } else {
                bot_b_debug = ExtractBotDebugText(search->info_lines);
            }
        }

        board = board.make_move(chosen_move);
        live_fen_history.push_back(ChessIO::board_to_fen(board));
        ++repetition_count[board.position_key()];
        outcome.final_board = board;
        on_update(board, status_note, bot_a_debug, bot_b_debug);
    }

    outcome.result = BenchResult::Draw;
    outcome.status_text = "Draw by ply cap";
    outcome.final_board = board;
    return outcome;
}

}  // namespace

@interface BenchBoardView : NSView
- (void)setBoardState:(const ChessBoard&)board;
@end

@implementation BenchBoardView {
    ChessBoard _board;
    NSMutableDictionary<NSString*, NSImage*>* _pieceImageCache;
    NSImage* _boardImage;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _pieceImageCache = [[NSMutableDictionary alloc] init];
        NSString* boardPath = FindBoardAssetPath();
        if (boardPath != nil) {
            _boardImage = [[NSImage alloc] initWithContentsOfFile:boardPath];
        }
        _board.initialize();
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (CGFloat)boardSize {
    return MIN(self.bounds.size.width, self.bounds.size.height);
}

- (CGFloat)cellSize {
    return [self boardSize] / 8.0;
}

- (NSImage*)imageForPiece:(Piece)piece {
    NSString* key = PieceCacheKey(piece, [self cellSize]);
    NSImage* image = [_pieceImageCache objectForKey:key];
    if (image == nil) {
        image = LoadPieceAsset(piece, [self cellSize]);
        if (image == nil) {
            image = DrawPieceImage(piece, [self cellSize]);
        }
        [_pieceImageCache setObject:image forKey:key];
    }
    return image;
}

- (void)setBoardState:(const ChessBoard&)board {
    _board = board;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;

    const CGFloat cell = [self cellSize];
    NSDictionary<NSAttributedStringKey, id>* rankFileAttributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.2 alpha:1.0]
    };

    const CGFloat boardSide = [self boardSize];
    const NSRect boardRect = NSMakeRect(0, 0, boardSide, boardSide);
    const int checkedKingSquare = _board.is_in_check(_board.turn) ? FindKingSquare(_board, _board.turn) : -1;

    if (_boardImage != nil) {
        [_boardImage drawInRect:boardRect
                       fromRect:NSZeroRect
                      operation:NSCompositingOperationSourceOver
                       fraction:1.0];
    }

    for (NSInteger boardRank = 7; boardRank >= 0; --boardRank) {
        for (NSInteger file = 0; file < 8; ++file) {
            const NSInteger square = boardRank * 8 + file;
            const NSInteger rowFromTop = 7 - boardRank;
            NSRect rect = NSMakeRect(file * cell, rowFromTop * cell, cell, cell);

            if (_boardImage == nil) {
                NSColor* fillColor = ((boardRank + file) % 2 == 0)
                    ? [NSColor colorWithCalibratedRed:0.95 green:0.91 blue:0.81 alpha:1.0]
                    : [NSColor colorWithCalibratedRed:0.71 green:0.53 blue:0.39 alpha:1.0];
                [fillColor setFill];
                NSRectFill(rect);
            }

            if (square == checkedKingSquare) {
                [[NSColor colorWithCalibratedRed:0.88 green:0.14 blue:0.18 alpha:0.48] setFill];
                NSBezierPath* checkedPath =
                    [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rect, 4.0, 4.0) xRadius:12.0 yRadius:12.0];
                [checkedPath fill];
            }

            const Piece piece = _board.piece_at(static_cast<int>(square));
            if (piece != EMPTY) {
                NSImage* image = [self imageForPiece:piece];
                const CGFloat imageSize = cell * kPieceScale;
                NSRect imageRect = NSMakeRect(rect.origin.x + (cell - imageSize) / 2.0,
                                              rect.origin.y + (cell - imageSize) / 2.0 + cell * kPieceYOffsetFactor,
                                              imageSize,
                                              imageSize);
                [image drawInRect:imageRect];
            }
        }
    }

    for (NSInteger file = 0; file < 8; ++file) {
        NSString* label = [NSString stringWithFormat:@"%c", static_cast<char>('a' + file)];
        [label drawAtPoint:NSMakePoint(file * cell + 6.0, [self boardSize] - 18.0)
           withAttributes:rankFileAttributes];
    }

    for (NSInteger rank = 0; rank < 8; ++rank) {
        NSString* label = [NSString stringWithFormat:@"%ld", static_cast<long>(8 - rank)];
        [label drawAtPoint:NSMakePoint(4.0, rank * cell + 4.0) withAttributes:rankFileAttributes];
    }
}

@end

@interface WDLBarView : NSView
- (void)setWins:(NSInteger)wins draws:(NSInteger)draws losses:(NSInteger)losses total:(NSInteger)total;
@end

@implementation WDLBarView {
    NSInteger _wins;
    NSInteger _draws;
    NSInteger _losses;
    NSInteger _total;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)setWins:(NSInteger)wins draws:(NSInteger)draws losses:(NSInteger)losses total:(NSInteger)total {
    _wins = wins;
    _draws = draws;
    _losses = losses;
    _total = MAX(total, 0);
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;

    const NSRect barRect = NSInsetRect(self.bounds, 0.5, 0.5);
    NSBezierPath* clipPath =
        [NSBezierPath bezierPathWithRoundedRect:barRect xRadius:10.0 yRadius:10.0];

    [[NSColor colorWithCalibratedWhite:0.95 alpha:1.0] setFill];
    [clipPath fill];

    [[NSColor colorWithCalibratedWhite:0.84 alpha:1.0] setStroke];
    [clipPath setLineWidth:1.0];
    [clipPath stroke];

    const NSInteger played = _wins + _draws + _losses;
    if (played <= 0) {
        return;
    }

    const CGFloat width = barRect.size.width;
    const CGFloat height = barRect.size.height;
    const CGFloat winWidth = width * static_cast<CGFloat>(_wins) / static_cast<CGFloat>(played);
    const CGFloat drawWidth = width * static_cast<CGFloat>(_draws) / static_cast<CGFloat>(played);
    const CGFloat lossWidth = width - winWidth - drawWidth;

    [NSGraphicsContext saveGraphicsState];
    [clipPath addClip];

    [[NSColor colorWithCalibratedRed:0.16 green:0.60 blue:0.33 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(barRect.origin.x, barRect.origin.y, winWidth, height));
    [[NSColor colorWithCalibratedRed:0.89 green:0.73 blue:0.23 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(barRect.origin.x + winWidth, barRect.origin.y, drawWidth, height));
    [[NSColor colorWithCalibratedRed:0.84 green:0.26 blue:0.21 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(barRect.origin.x + winWidth + drawWidth, barRect.origin.y, lossWidth, height));
    [NSGraphicsContext restoreGraphicsState];

    NSDictionary<NSAttributedStringKey, id>* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: NSColor.whiteColor
    };

    NSString* text = [NSString stringWithFormat:@"W %ld   D %ld   L %ld",
                      static_cast<long>(_wins),
                      static_cast<long>(_draws),
                      static_cast<long>(_losses)];
    [text drawAtPoint:NSMakePoint(barRect.origin.x + 12.0, barRect.origin.y + 8.0) withAttributes:attrs];
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow* _window;
    BenchBoardView* _boardView;
    WDLBarView* _wdlBarView;
    NSTextField* _headlineLabel;
    NSTextField* _statusLabel;
    NSTextView* _detailsView;
    NSTextView* _botADebugView;
    NSTextView* _botBDebugView;
    NSButton* _startButton;
    NSTextField* _datasetField;
    NSTextField* _botAFolderField;
    NSTextField* _botBFolderField;
    NSTextField* _botATimeField;
    NSTextField* _botBTimeField;
    BOOL _isRunning;
    NSInteger _wins;
    NSInteger _draws;
    NSInteger _losses;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    [self installMainMenu];

    const NSRect frame = NSMakeRect(0, 0, 1260, 760);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable |
                                                     NSWindowStyleMaskResizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window center];
    [_window setTitle:@"Chess Bench"];

    NSView* contentView = [_window contentView];

    _wdlBarView = [[WDLBarView alloc] initWithFrame:NSMakeRect(20, 696, 1190, 40)];
    [_wdlBarView setWins:0 draws:0 losses:0 total:0];
    [contentView addSubview:_wdlBarView];

    _boardView = [[BenchBoardView alloc] initWithFrame:NSMakeRect(20, 20, 640, 640)];
    [_boardView setWantsLayer:YES];
    _boardView.layer.backgroundColor = NSColor.whiteColor.CGColor;
    [contentView addSubview:_boardView];

    _headlineLabel = [NSTextField labelWithString:@"Bot folder vs bot folder bench"];
    [_headlineLabel setFont:[NSFont systemFontOfSize:22 weight:NSFontWeightBold]];
    [_headlineLabel setFrame:NSMakeRect(690, 652, 520, 28)];
    [contentView addSubview:_headlineLabel];

    NSTextField* datasetLabel = [NSTextField labelWithString:@"Dataset"];
    [datasetLabel setFrame:NSMakeRect(690, 636, 70, 20)];
    [contentView addSubview:datasetLabel];

    _datasetField = [[NSTextField alloc] initWithFrame:NSMakeRect(690, 612, 430, 24)];
    [_datasetField setStringValue:[NSString stringWithUTF8String:DefaultDatasetPath().c_str()]];
    [_datasetField setEditable:YES];
    [_datasetField setSelectable:YES];
    [contentView addSubview:_datasetField];

    NSButton* chooseDatasetButton = [[NSButton alloc] initWithFrame:NSMakeRect(1130, 608, 80, 32)];
    [chooseDatasetButton setTitle:@"Choose"];
    [chooseDatasetButton setBezelStyle:NSBezelStyleRounded];
    [chooseDatasetButton setTarget:self];
    [chooseDatasetButton setAction:@selector(chooseDatasetFile:)];
    [contentView addSubview:chooseDatasetButton];

    NSTextField* botALabel = [NSTextField labelWithString:@"Bot A folder"];
    [botALabel setFrame:NSMakeRect(690, 578, 100, 20)];
    [contentView addSubview:botALabel];

    _botAFolderField = [[NSTextField alloc] initWithFrame:NSMakeRect(690, 554, 430, 24)];
    [_botAFolderField setStringValue:[NSString stringWithUTF8String:DefaultBotFolderFromEnv("CHESS_BENCH_BOT_A", "bots/v1_baseline").c_str()]];
    [_botAFolderField setEditable:YES];
    [_botAFolderField setSelectable:YES];
    [contentView addSubview:_botAFolderField];

    NSButton* chooseBotAButton = [[NSButton alloc] initWithFrame:NSMakeRect(1130, 550, 80, 32)];
    [chooseBotAButton setTitle:@"Choose"];
    [chooseBotAButton setBezelStyle:NSBezelStyleRounded];
    [chooseBotAButton setTarget:self];
    [chooseBotAButton setAction:@selector(chooseBotAFolder:)];
    [contentView addSubview:chooseBotAButton];

    NSTextField* botBLabel = [NSTextField labelWithString:@"Bot B folder"];
    [botBLabel setFrame:NSMakeRect(690, 522, 100, 20)];
    [contentView addSubview:botBLabel];

    _botBFolderField = [[NSTextField alloc] initWithFrame:NSMakeRect(690, 498, 430, 24)];
    [_botBFolderField setStringValue:[NSString stringWithUTF8String:DefaultBotFolderFromEnv("CHESS_BENCH_BOT_B", "").c_str()]];
    [_botBFolderField setEditable:YES];
    [_botBFolderField setSelectable:YES];
    [contentView addSubview:_botBFolderField];

    NSButton* chooseBotBButton = [[NSButton alloc] initWithFrame:NSMakeRect(1130, 494, 80, 32)];
    [chooseBotBButton setTitle:@"Choose"];
    [chooseBotBButton setBezelStyle:NSBezelStyleRounded];
    [chooseBotBButton setTarget:self];
    [chooseBotBButton setAction:@selector(chooseBotBFolder:)];
    [contentView addSubview:chooseBotBButton];

    NSTextField* botATimeLabel = [NSTextField labelWithString:@"Bot A ms/move"];
    [botATimeLabel setFrame:NSMakeRect(690, 456, 110, 20)];
    [contentView addSubview:botATimeLabel];

    _botATimeField = [[NSTextField alloc] initWithFrame:NSMakeRect(690, 430, 110, 24)];
    [_botATimeField setStringValue:@"50"];
    [_botATimeField setEditable:YES];
    [_botATimeField setSelectable:YES];
    [contentView addSubview:_botATimeField];

    NSTextField* botBTimeLabel = [NSTextField labelWithString:@"Bot B ms/move"];
    [botBTimeLabel setFrame:NSMakeRect(850, 456, 110, 20)];
    [contentView addSubview:botBTimeLabel];

    _botBTimeField = [[NSTextField alloc] initWithFrame:NSMakeRect(850, 430, 110, 24)];
    [_botBTimeField setStringValue:@"50"];
    [_botBTimeField setEditable:YES];
    [_botBTimeField setSelectable:YES];
    [contentView addSubview:_botBTimeField];

    _startButton = [[NSButton alloc] initWithFrame:NSMakeRect(1070, 386, 140, 32)];
    [_startButton setTitle:@"Start Bench"];
    [_startButton setBezelStyle:NSBezelStyleRounded];
    [_startButton setTarget:self];
    [_startButton setAction:@selector(startBench:)];
    [contentView addSubview:_startButton];

    _statusLabel = [NSTextField labelWithString:@"Ready. Choose two bot folders with bot.json manifests, then start the bench."];
    [_statusLabel setFrame:NSMakeRect(0, 0, 1, 1)];
    [_statusLabel setHidden:YES];

    NSTextField* botADebugLabel = [NSTextField labelWithString:@"Bot A debug"];
    [botADebugLabel setFrame:NSMakeRect(690, 320, 120, 18)];
    [contentView addSubview:botADebugLabel];

    NSTextField* botBDebugLabel = [NSTextField labelWithString:@"Bot B debug"];
    [botBDebugLabel setFrame:NSMakeRect(960, 320, 120, 18)];
    [contentView addSubview:botBDebugLabel];

    NSScrollView* botADebugScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(690, 248, 250, 64)];
    [botADebugScroll setHasVerticalScroller:YES];
    [botADebugScroll setBorderType:NSBezelBorder];
    _botADebugView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 250, 64)];
    [_botADebugView setEditable:NO];
    [_botADebugView setFont:[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]];
    [_botADebugView setString:@"Waiting for move..."];
    [botADebugScroll setDocumentView:_botADebugView];
    [contentView addSubview:botADebugScroll];

    NSScrollView* botBDebugScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(960, 248, 250, 64)];
    [botBDebugScroll setHasVerticalScroller:YES];
    [botBDebugScroll setBorderType:NSBezelBorder];
    _botBDebugView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 250, 64)];
    [_botBDebugView setEditable:NO];
    [_botBDebugView setFont:[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]];
    [_botBDebugView setString:@"Waiting for move..."];
    [botBDebugScroll setDocumentView:_botBDebugView];
    [contentView addSubview:botBDebugScroll];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(690, 20, 520, 216)];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setBorderType:NSBezelBorder];
    _detailsView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 520, 216)];
    [_detailsView setEditable:NO];
    [_detailsView setFont:[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]];
    [scrollView setDocumentView:_detailsView];
    [contentView addSubview:scrollView];

    [_window makeKeyAndOrderFront:nil];

    const char* autostart = std::getenv("CHESS_BENCH_AUTOSTART");
    if (autostart != nullptr && autostart[0] != '\0') {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self startBench:nil];
        });
    }
}

- (void)setStatus:(NSString*)text {
    [_statusLabel setStringValue:text ?: @""];
}

- (void)installMainMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [mainMenu addItem:appMenuItem];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Chess Bench"];
    NSString* appName = [[NSProcessInfo processInfo] processName];
    NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
                                                      action:@selector(terminate:)
                                               keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [mainMenu setSubmenu:appMenu forItem:appMenuItem];

    NSMenuItem* editMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [mainMenu addItem:editMenuItem];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"]];
    NSMenuItem* redoItem = [[NSMenuItem alloc] initWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
    [redoItem setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [editMenu addItem:redoItem];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"]];
    [editMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"]];
    [mainMenu setSubmenu:editMenu forItem:editMenuItem];

    [NSApp setMainMenu:mainMenu];
}

- (void)chooseDatasetFile:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] == NSModalResponseOK) {
        NSURL* url = panel.URL;
        if (url != nil) {
            [_datasetField setStringValue:MakePathDisplayString(url.path ?: @"")];
        }
    }
}

- (void)chooseFolderForField:(NSTextField*)field {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:YES];
    [panel setCanChooseFiles:NO];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] == NSModalResponseOK) {
        NSURL* url = panel.URL;
        if (url != nil) {
            [field setStringValue:MakePathDisplayString(url.path ?: @"")];
        }
    }
}

- (void)chooseBotAFolder:(id)sender {
    (void)sender;
    [self chooseFolderForField:_botAFolderField];
}

- (void)chooseBotBFolder:(id)sender {
    (void)sender;
    [self chooseFolderForField:_botBFolderField];
}

- (void)updateDetailsWithPosition:(const BenchDatasetPosition&)position
                         botAName:(const std::string&)bot_a_name
                         botBName:(const std::string&)bot_b_name
                    botAColorName:(const std::string&)bot_a_color_name
                          liveFen:(const std::string&)live_fen
                       liveStatus:(const std::string&)live_status
                        gameIndex:(NSInteger)game_index
                       totalGames:(NSInteger)total_games {
    const std::string details = BuildPositionDetails(position,
                                                     bot_a_name,
                                                     bot_b_name,
                                                     bot_a_color_name,
                                                     live_fen,
                                                     live_status,
                                                     static_cast<int>(game_index),
                                                     static_cast<int>(total_games));
    [_detailsView setString:[NSString stringWithUTF8String:details.c_str()]];
}

- (void)applyBoardFenString:(NSString*)fen
                    details:(NSString*)details
                     status:(NSString*)status
                  botADebug:(NSString*)botADebug
                  botBDebug:(NSString*)botBDebug {
    if (fen != nil) {
        try {
            const std::string fen_text = std::string([fen UTF8String]);
            ChessBoard board{fen_text};
            [_boardView setBoardState:board];
        } catch (...) {
        }
    }
    if (details != nil) {
        [_detailsView setString:details];
    }
    if (status != nil) {
        [_statusLabel setStringValue:status];
    }
    if (botADebug != nil) {
        [_botADebugView setString:botADebug];
    }
    if (botBDebug != nil) {
        [_botBDebugView setString:botBDebug];
    }
}

- (void)startBench:(id)sender {
    (void)sender;
    BenchLog("startBench: entered");
    if (_isRunning) {
        BenchLog("startBench: already running");
        return;
    }

    const std::string dataset_input([_datasetField.stringValue UTF8String]);
    const std::string bot_a_folder_input([_botAFolderField.stringValue UTF8String]);
    const std::string bot_b_folder_input([_botBFolderField.stringValue UTF8String]);
    const std::string dataset_path = ResolveUserPath(dataset_input, false);
    const std::string bot_a_folder = ResolveUserPath(bot_a_folder_input, true);
    const std::string bot_b_folder = ResolveUserPath(bot_b_folder_input, true);
    BenchLog("startBench: dataset input=" + dataset_input);
    BenchLog("startBench: botA input=" + bot_a_folder_input);
    BenchLog("startBench: botB input=" + bot_b_folder_input);
    BenchLog("startBench: dataset resolved=" + dataset_path);
    BenchLog("startBench: botA resolved=" + bot_a_folder);
    BenchLog("startBench: botB resolved=" + bot_b_folder);
    if (dataset_path.empty()) {
        BenchLog("startBench: dataset empty");
        [self setStatus:@"Dataset path is empty."];
        return;
    }
    if (bot_a_folder.empty() || bot_b_folder.empty()) {
        BenchLog("startBench: bot folder missing");
        [self setStatus:@"Both Bot A and Bot B folders are required."];
        return;
    }

    std::vector<BenchDatasetPosition> positions;
    std::string error;
    if (!LoadDatasetFile(dataset_path, positions, error)) {
        BenchLog("startBench: failed to load dataset: " + error);
        [self setStatus:[NSString stringWithFormat:@"Failed to load dataset: %s", error.c_str()]];
        return;
    }
    BenchLog("startBench: dataset loaded positions=" + std::to_string(positions.size()));

    BenchBotSpec bot_a_spec;
    BenchBotSpec bot_b_spec;
    if (!LoadBotSpecFromFolder(bot_a_folder, bot_a_spec, error)) {
        BenchLog("startBench: failed to load botA: " + error);
        [self setStatus:[NSString stringWithFormat:@"Failed to load Bot A: %s", error.c_str()]];
        return;
    }
    if (!LoadBotSpecFromFolder(bot_b_folder, bot_b_spec, error)) {
        BenchLog("startBench: failed to load botB: " + error);
        [self setStatus:[NSString stringWithFormat:@"Failed to load Bot B: %s", error.c_str()]];
        return;
    }
    BenchLog("startBench: bot specs loaded");

    const int bot_a_time_ms = MAX(1, _botATimeField.intValue);
    const int bot_b_time_ms = MAX(1, _botBTimeField.intValue);

    _isRunning = YES;
    _wins = 0;
    _draws = 0;
    _losses = 0;
    [_wdlBarView setWins:0 draws:0 losses:0 total:static_cast<NSInteger>(positions.size())];
    [_startButton setEnabled:NO];
    [_botADebugView setString:@"Waiting for move..."];
    [_botBDebugView setString:@"Waiting for move..."];
    [self setStatus:[NSString stringWithFormat:@"Loaded %lu positions. Starting bench...",
                                               static_cast<unsigned long>(positions.size())]];

    __weak AppDelegate* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BenchLog("worker: started");
        AppDelegate* strongSelf = weakSelf;
        if (strongSelf == nil) {
            BenchLog("worker: self nil");
            return;
        }

        try {
            std::string bot_error;
            BotProcess botA({bot_a_spec.display_name,
                             bot_a_spec.executable_path,
                             bot_a_spec.args,
                             bot_a_spec.working_directory});
            BotProcess botB({bot_b_spec.display_name,
                             bot_b_spec.executable_path,
                             bot_b_spec.args,
                             bot_b_spec.working_directory});
            BenchLog("worker: bot processes created");
            if (!botA.start(&bot_error) || !botB.start(&bot_error)) {
                const std::string start_error = bot_error;
                BenchLog("worker: failed to start bot process: " + start_error);
                dispatch_async(dispatch_get_main_queue(), ^{
                    AppDelegate* innerSelf = weakSelf;
                    if (innerSelf == nil) {
                        return;
                    }
                    innerSelf->_isRunning = NO;
                    [innerSelf->_startButton setEnabled:YES];
                    [innerSelf setStatus:[NSString stringWithFormat:@"Failed to start bot process: %s",
                                                                 start_error.c_str()]];
                });
                return;
            }
            BenchLog("worker: bot processes started");

            for (std::size_t index = 0; index < positions.size(); ++index) {
                BenchLog("worker: game loop index=" + std::to_string(index));
                const BenchDatasetPosition& position = positions[index];
                const BenchDatasetPosition position_copy = position;
                ChessBoard starting_board(position.selected_fen);
                const bool botAOnTurnSide = (index % 2) == 0;
                const std::string botAColorName =
                    (botAOnTurnSide ? starting_board.turn : (starting_board.turn == WHITE ? BLACK : WHITE)) == WHITE
                        ? "White"
                    : "Black";
                const std::string botAName = bot_a_spec.display_name;
                const std::string botBName = bot_b_spec.display_name;
                const NSInteger currentGameIndex = static_cast<NSInteger>(index + 1);
                const NSInteger totalGames = static_cast<NSInteger>(positions.size());
                std::string latestBotADebug = "Waiting for move...";
                std::string latestBotBDebug = "Waiting for move...";
                const std::string start_details_text = BuildPositionDetails(position_copy,
                                                                            botAName,
                                                                            botBName,
                                                                            botAColorName,
                                                                            position_copy.selected_fen,
                                                                            "Waiting to start this game",
                                                                            static_cast<int>(currentGameIndex),
                                                                            static_cast<int>(totalGames));
                NSString* start_fen = [NSString stringWithUTF8String:position_copy.selected_fen.c_str()];
                NSString* start_details = [NSString stringWithUTF8String:start_details_text.c_str()];
                NSString* start_status = [NSString stringWithFormat:@"Running game %lu of %lu...",
                                          static_cast<unsigned long>(currentGameIndex),
                                          static_cast<unsigned long>(totalGames)];
                NSString* start_bot_a_debug = @"Waiting for move...";
                NSString* start_bot_b_debug = @"Waiting for move...";
                if (!botA.new_game(&bot_error) || !botB.new_game(&bot_error)) {
                    const std::string reset_error = bot_error;
                    BenchLog("worker: failed to reset bots: " + reset_error);
                    dispatch_async(dispatch_get_main_queue(), ^{
                        AppDelegate* innerSelf = weakSelf;
                        if (innerSelf == nil) {
                            return;
                        }
                        innerSelf->_isRunning = NO;
                        [innerSelf->_startButton setEnabled:YES];
                        [innerSelf setStatus:[NSString stringWithFormat:@"Failed to reset bots: %s",
                                                                     reset_error.c_str()]];
                    });
                    return;
                }

                dispatch_async(dispatch_get_main_queue(), ^{
                    AppDelegate* innerSelf = weakSelf;
                    if (innerSelf == nil) {
                        return;
                    }
                    [innerSelf applyBoardFenString:start_fen
                                           details:start_details
                                            status:start_status
                                         botADebug:start_bot_a_debug
                                         botBDebug:start_bot_b_debug];
                });

                BenchLog("worker: before PlayBenchMatch");
                const BenchOutcome outcome = PlayBenchMatch(position,
                                                            botA,
                                                            bot_a_time_ms,
                                                            botB,
                                                            bot_b_time_ms,
                                                            botAOnTurnSide,
                                                            [&](const ChessBoard& board,
                                                                const std::string& note,
                                                                const std::string& botADebugText,
                                                                const std::string& botBDebugText) {
                    latestBotADebug = botADebugText;
                    latestBotBDebug = botBDebugText;
                    const std::string board_fen = ChessIO::board_to_fen(board);
                    const std::string details_text = BuildPositionDetails(position_copy,
                                                                          botAName,
                                                                          botBName,
                                                                          botAColorName,
                                                                          board_fen,
                                                                          note,
                                                                          static_cast<int>(currentGameIndex),
                                                                          static_cast<int>(totalGames));
                    NSString* fen = [NSString stringWithUTF8String:board_fen.c_str()];
                    NSString* details = [NSString stringWithUTF8String:details_text.c_str()];
                    NSString* status = [NSString stringWithUTF8String:note.c_str()];
                    NSString* botADebug = [NSString stringWithUTF8String:botADebugText.c_str()];
                    NSString* botBDebug = [NSString stringWithUTF8String:botBDebugText.c_str()];
                    dispatch_async(dispatch_get_main_queue(), ^{
                        AppDelegate* innerSelf = weakSelf;
                        if (innerSelf == nil) {
                            return;
                        }
                        [innerSelf applyBoardFenString:fen
                                               details:details
                                                status:status
                                             botADebug:botADebug
                                             botBDebug:botBDebug];
                    });
                });
                BenchLog("worker: after PlayBenchMatch");

                const BenchOutcome outcome_copy = outcome;
                const std::string outcome_fen = ChessIO::board_to_fen(outcome_copy.final_board);
                const std::string outcome_details = BuildPositionDetails(position_copy,
                                                                         botAName,
                                                                         botBName,
                                                                         botAColorName,
                                                                         outcome_fen,
                                                                         outcome_copy.status_text,
                                                                         static_cast<int>(currentGameIndex),
                                                                         static_cast<int>(totalGames));
                std::string outcome_status_text =
                    "Game " + std::to_string(currentGameIndex) + "/" + std::to_string(totalGames) +
                    " finished: " + outcome_copy.status_text;
                if (!outcome_copy.error_text.empty()) {
                    outcome_status_text += " (" + outcome_copy.error_text + ")";
                }
                NSString* outcome_fen_ns = [NSString stringWithUTF8String:outcome_fen.c_str()];
                NSString* outcome_details_ns = [NSString stringWithUTF8String:outcome_details.c_str()];
                NSString* outcome_status_ns = [NSString stringWithUTF8String:outcome_status_text.c_str()];
                NSString* outcome_bot_a_debug = [NSString stringWithUTF8String:latestBotADebug.c_str()];
                NSString* outcome_bot_b_debug = [NSString stringWithUTF8String:latestBotBDebug.c_str()];
                dispatch_async(dispatch_get_main_queue(), ^{
                    AppDelegate* innerSelf = weakSelf;
                    if (innerSelf == nil) {
                        return;
                    }
                    [innerSelf applyBoardFenString:outcome_fen_ns
                                           details:outcome_details_ns
                                            status:outcome_status_ns
                                         botADebug:outcome_bot_a_debug
                                         botBDebug:outcome_bot_b_debug];

                    switch (outcome_copy.result) {
                        case BenchResult::Win:
                            innerSelf->_wins += 1;
                            break;
                        case BenchResult::Draw:
                            innerSelf->_draws += 1;
                            break;
                        case BenchResult::Loss:
                        case BenchResult::Error:
                            innerSelf->_losses += 1;
                            break;
                    }

                    [innerSelf->_wdlBarView setWins:innerSelf->_wins
                                              draws:innerSelf->_draws
                                             losses:innerSelf->_losses
                                               total:totalGames];
                });
            }

            dispatch_async(dispatch_get_main_queue(), ^{
                AppDelegate* innerSelf = weakSelf;
                if (innerSelf == nil) {
                    return;
                }
                innerSelf->_isRunning = NO;
                [innerSelf->_startButton setEnabled:YES];
                [innerSelf setStatus:[NSString stringWithFormat:@"Bench complete. W %ld | D %ld | L %ld",
                                                           static_cast<long>(innerSelf->_wins),
                                                           static_cast<long>(innerSelf->_draws),
                                                           static_cast<long>(innerSelf->_losses)]];
            });
        } catch (const std::exception& ex) {
            const std::string exception_text = ex.what();
            BenchLog(std::string("worker: exception: ") + exception_text);
            dispatch_async(dispatch_get_main_queue(), ^{
                AppDelegate* innerSelf = weakSelf;
                if (innerSelf == nil) {
                    return;
                }
                innerSelf->_isRunning = NO;
                [innerSelf->_startButton setEnabled:YES];
                [innerSelf setStatus:[NSString stringWithFormat:@"Bench exception: %s", exception_text.c_str()]];
            });
        } catch (...) {
            BenchLog("worker: unknown exception");
            dispatch_async(dispatch_get_main_queue(), ^{
                AppDelegate* innerSelf = weakSelf;
                if (innerSelf == nil) {
                    return;
                }
                innerSelf->_isRunning = NO;
                [innerSelf->_startButton setEnabled:YES];
                [innerSelf setStatus:@"Bench hit an unknown exception."];
            });
        }
    });
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

int RunChessBenchApp() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
