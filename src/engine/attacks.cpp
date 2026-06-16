#include "chess/attacks.h"

#include "chessboard_internal.h"

#include <array>
#include <cstddef>

namespace {

constexpr std::size_t kMaxBishopBlockerSubsets = 1u << 9;
constexpr std::size_t kMaxRookBlockerSubsets = 1u << 12;

constexpr Bitboard square_bit(int square) {
    return 1ULL << square;
}

constexpr int rank_of(int square) {
    return square / 8;
}

constexpr int file_of(int square) {
    return square % 8;
}

constexpr Bitboard build_knight_attacks(int square) {
    constexpr int kKnightDeltas[8][2] = {
        {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
        {-2, -1}, {-1, -2}, {1, -2}, {2, -1},
    };

    const int rank = rank_of(square);
    const int file = file_of(square);
    Bitboard attacks = 0;
    for (const auto& delta : kKnightDeltas) {
        const int target_rank = rank + delta[0];
        const int target_file = file + delta[1];
        if (target_rank < 0 || target_rank >= 8 || target_file < 0 || target_file >= 8) {
            continue;
        }
        attacks |= square_bit(target_rank * 8 + target_file);
    }
    return attacks;
}

constexpr Bitboard build_king_attacks(int square) {
    constexpr int kKingDeltas[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
    };

    const int rank = rank_of(square);
    const int file = file_of(square);
    Bitboard attacks = 0;
    for (const auto& delta : kKingDeltas) {
        const int target_rank = rank + delta[0];
        const int target_file = file + delta[1];
        if (target_rank < 0 || target_rank >= 8 || target_file < 0 || target_file >= 8) {
            continue;
        }
        attacks |= square_bit(target_rank * 8 + target_file);
    }
    return attacks;
}

constexpr std::array<Bitboard, 64> build_knight_attack_table() {
    std::array<Bitboard, 64> table{};
    for (int square = 0; square < 64; ++square) {
        table[static_cast<std::size_t>(square)] = build_knight_attacks(square);
    }
    return table;
}

constexpr std::array<Bitboard, 64> build_king_attack_table() {
    std::array<Bitboard, 64> table{};
    for (int square = 0; square < 64; ++square) {
        table[static_cast<std::size_t>(square)] = build_king_attacks(square);
    }
    return table;
}

constexpr std::array<Bitboard, 64> kKnightAttackTable = build_knight_attack_table();
constexpr std::array<Bitboard, 64> kKingAttackTable = build_king_attack_table();
constexpr std::array<Bitboard, 64> kBishopMagics = {
    0x0020e01100408080ULL, 0x2e08080810802084ULL, 0x0008022046000040ULL, 0x80444040800a0000ULL,
    0x0102021118402050ULL, 0x00a1301010046000ULL, 0x0002580444600021ULL, 0x0082105402201000ULL,
    0x4460200441084104ULL, 0x804120224c010029ULL, 0x002022080100200cULL, 0x1cc0040400845046ULL,
    0x0001040420001000ULL, 0x1012120824442601ULL, 0x000002582404a008ULL, 0x0000018948061080ULL,
    0x1488680408104401ULL, 0x0020804208010100ULL, 0x8008224040810010ULL, 0x100208a02201c000ULL,
    0x480900a820080002ULL, 0x424101021000a400ULL, 0x8222008082192100ULL, 0x42020400250c0280ULL,
    0xa008840c60200220ULL, 0x8050840010046080ULL, 0x4100404044040082ULL, 0x0020080081004008ULL,
    0x0001080421004000ULL, 0x00008101160100c0ULL, 0x400204c014340a00ULL, 0x0001150800440080ULL,
    0x0002100600400831ULL, 0x0082080408202180ULL, 0x0120814444100404ULL, 0x0000020080180080ULL,
    0x0030020080001004ULL, 0xc420028d81010810ULL, 0x000c2806020a4110ULL, 0x0008410020110083ULL,
    0x40040c2009002400ULL, 0x20210808420024a9ULL, 0x0000104030088800ULL, 0x8080b02011009808ULL,
    0x4000042810102201ULL, 0x200400c092005101ULL, 0x002812080a080040ULL, 0x1010020a002c0045ULL,
    0x0012021002081080ULL, 0x0190848808220480ULL, 0x41c8802201500280ULL, 0x0000108042020000ULL,
    0x00000042d0410028ULL, 0x0010200410209200ULL, 0x00a2321001010401ULL, 0x0ca0080210504010ULL,
    0x8005108a08024081ULL, 0x400082248a1010a2ULL, 0x0014500042081140ULL, 0x0081400100420220ULL,
    0x0000000040104102ULL, 0x0840040810500220ULL, 0x2002200450021850ULL, 0x084a200404029024ULL,
};
constexpr std::array<std::uint8_t, 64> kBishopShifts = {
    58, 59, 59, 59, 59, 59, 59, 58, 59, 59, 59, 59, 59, 59, 59, 59,
    59, 59, 57, 57, 57, 57, 59, 59, 59, 59, 57, 55, 55, 57, 59, 59,
    59, 59, 57, 55, 55, 57, 59, 59, 59, 59, 57, 57, 57, 57, 59, 59,
    59, 59, 59, 59, 59, 59, 59, 59, 58, 59, 59, 59, 59, 59, 59, 58,
};
constexpr std::array<Bitboard, 64> kRookMagics = {
    0x2080001040042288ULL, 0x0040100040002000ULL, 0x0200208008420011ULL, 0x4080040800100080ULL,
    0x0a00041002002008ULL, 0x0080040002008001ULL, 0x6080800100008200ULL, 0x0100008022004100ULL,
    0x20c1800040008020ULL, 0xc000404010002000ULL, 0x0041801000a00080ULL, 0x1010800800801002ULL,
    0x0011000800110004ULL, 0x0006004810020004ULL, 0x0134001411181042ULL, 0x505900060280c100ULL,
    0x4200228000400080ULL, 0x001010c000406000ULL, 0x0001010040102000ULL, 0x0008808010000800ULL,
    0x0018008008040081ULL, 0x0011808004010200ULL, 0x0280040088021001ULL, 0x04a4020003225084ULL,
    0x9020800080204002ULL, 0x4501500140002006ULL, 0x0400401100200100ULL, 0x10021001000b0020ULL,
    0x0000080080040082ULL, 0xd083000300080400ULL, 0x0041002900140200ULL, 0x6000412200008044ULL,
    0x4080006000c00040ULL, 0x0010024002402004ULL, 0x010a104202002080ULL, 0x0020814802803000ULL,
    0x2010800800800400ULL, 0x0052000401010008ULL, 0x0800100104006882ULL, 0x0040010082000064ULL,
    0x0090804000208000ULL, 0x0020200040008080ULL, 0x0502031081420024ULL, 0x0010100008008080ULL,
    0xa008008004008008ULL, 0x0002000400808002ULL, 0x0042020001008080ULL, 0x6001001480450012ULL,
    0x00800120004000c0ULL, 0x0440002040890100ULL, 0x4000102001004100ULL, 0x8480100080080480ULL,
    0x0000080004110100ULL, 0x25a0020004008080ULL, 0x0044900102080400ULL, 0x0001000082004100ULL,
    0x0100201102004082ULL, 0x8100104000882105ULL, 0x0040081040228202ULL, 0x005000a004881101ULL,
    0xe082003508601012ULL, 0x5602000104c81002ULL, 0x0340100801008204ULL, 0x10001c0081002042ULL,
};
constexpr std::array<std::uint8_t, 64> kRookShifts = {
    52, 53, 53, 53, 53, 53, 53, 52, 53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53, 53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53, 52, 53, 53, 53, 53, 53, 53, 52,
};

Bitboard bishop_attacks_slow(int square, Bitboard occupied) {
    Bitboard attacks = 0;
    const int rank = row_of(square);
    const int file = col_of(square);
    static constexpr int kDirections[4][2] = {
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
    };

    for (const auto& direction : kDirections) {
        int target_rank = rank + direction[0];
        int target_file = file + direction[1];
        while (target_rank >= 0 &&
               target_rank < 8 &&
               target_file >= 0 &&
               target_file < 8) {
            const int target = target_rank * 8 + target_file;
            const Bitboard bit = square_bb(target);
            attacks |= bit;
            if (occupied & bit) {
                break;
            }
            target_rank += direction[0];
            target_file += direction[1];
        }
    }

    return attacks;
}

Bitboard rook_attacks_slow(int square, Bitboard occupied) {
    Bitboard attacks = 0;
    const int rank = row_of(square);
    const int file = col_of(square);
    static constexpr int kDirections[4][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    };

    for (const auto& direction : kDirections) {
        int target_rank = rank + direction[0];
        int target_file = file + direction[1];
        while (target_rank >= 0 &&
               target_rank < 8 &&
               target_file >= 0 &&
               target_file < 8) {
            const int target = target_rank * 8 + target_file;
            const Bitboard bit = square_bb(target);
            attacks |= bit;
            if (occupied & bit) {
                break;
            }
            target_rank += direction[0];
            target_file += direction[1];
        }
    }

    return attacks;
}

Bitboard build_bishop_mask(int square) {
    Bitboard mask = 0;
    const int rank = row_of(square);
    const int file = col_of(square);
    static constexpr int kDirections[4][2] = {
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
    };

    for (const auto& direction : kDirections) {
        int target_rank = rank + direction[0];
        int target_file = file + direction[1];
        while (target_rank > 0 &&
               target_rank < 7 &&
               target_file > 0 &&
               target_file < 7) {
            mask |= square_bb(target_rank * 8 + target_file);
            target_rank += direction[0];
            target_file += direction[1];
        }
    }

    return mask;
}

Bitboard build_rook_mask(int square) {
    Bitboard mask = 0;
    const int rank = row_of(square);
    const int file = col_of(square);
    static constexpr int kDirections[4][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    };

    for (const auto& direction : kDirections) {
        int target_rank = rank + direction[0];
        int target_file = file + direction[1];
        while (target_rank >= 0 &&
               target_rank < 8 &&
               target_file >= 0 &&
               target_file < 8) {
            const int next_rank = target_rank + direction[0];
            const int next_file = target_file + direction[1];
            if (next_rank < 0 || next_rank >= 8 || next_file < 0 || next_file >= 8) {
                break;
            }
            mask |= square_bb(target_rank * 8 + target_file);
            target_rank += direction[0];
            target_file += direction[1];
        }
    }

    return mask;
}

Bitboard subset_from_index(int index, Bitboard mask) {
    Bitboard subset = 0;
    int bit_index = 0;
    Bitboard remaining_mask = mask;
    while (remaining_mask != 0) {
        const int square = pop_lsb(remaining_mask);
        if ((index & (1 << bit_index)) != 0) {
            subset |= square_bb(square);
        }
        ++bit_index;
    }
    return subset;
}

struct SlidingAttackTables {
    std::array<Bitboard, 64> bishop_masks{};
    std::array<Bitboard, 64> rook_masks{};
    std::array<std::array<Bitboard, kMaxBishopBlockerSubsets>, 64> bishop_attacks{};
    std::array<std::array<Bitboard, kMaxRookBlockerSubsets>, 64> rook_attacks{};
};

const SlidingAttackTables& sliding_attack_tables() {
    static const SlidingAttackTables tables = [] {
        SlidingAttackTables generated{};
        for (int square = 0; square < 64; ++square) {
            const Bitboard bishop_mask = build_bishop_mask(square);
            const Bitboard rook_mask = build_rook_mask(square);
            generated.bishop_masks[static_cast<std::size_t>(square)] = bishop_mask;
            generated.rook_masks[static_cast<std::size_t>(square)] = rook_mask;

            const int bishop_shift = kBishopShifts[static_cast<std::size_t>(square)];
            const int rook_shift = kRookShifts[static_cast<std::size_t>(square)];
            const Bitboard bishop_magic = kBishopMagics[static_cast<std::size_t>(square)];
            const Bitboard rook_magic = kRookMagics[static_cast<std::size_t>(square)];
            const int bishop_bits = 64 - bishop_shift;
            const int rook_bits = 64 - rook_shift;
            const int bishop_subset_count = 1 << bishop_bits;
            const int rook_subset_count = 1 << rook_bits;

            for (int index = 0; index < bishop_subset_count; ++index) {
                const Bitboard occupied = subset_from_index(index, bishop_mask);
                const std::size_t magic_index = static_cast<std::size_t>(
                    (occupied * bishop_magic) >> bishop_shift);
                generated.bishop_attacks[static_cast<std::size_t>(square)][magic_index] =
                    bishop_attacks_slow(square, occupied);
            }

            for (int index = 0; index < rook_subset_count; ++index) {
                const Bitboard occupied = subset_from_index(index, rook_mask);
                const std::size_t magic_index =
                    static_cast<std::size_t>((occupied * rook_magic) >> rook_shift);
                generated.rook_attacks[static_cast<std::size_t>(square)][magic_index] =
                    rook_attacks_slow(square, occupied);
            }
        }
        return generated;
    }();
    return tables;
}

}  // namespace

Bitboard knight_attacks(int square) {
    return kKnightAttackTable[static_cast<std::size_t>(square)];
}

Bitboard king_attacks(int square) {
    return kKingAttackTable[static_cast<std::size_t>(square)];
}

Bitboard pawn_attacks(Bitboard pawns, Color color) {
    if (color == WHITE) {
        return ((pawns & ~kFileAMask) << 7) | ((pawns & ~kFileHMask) << 9);
    }

    return ((pawns & ~kFileAMask) >> 9) | ((pawns & ~kFileHMask) >> 7);
}

Bitboard bishop_attacks(int square, Bitboard occupied) {
    const SlidingAttackTables& tables = sliding_attack_tables();
    const Bitboard mask = tables.bishop_masks[static_cast<std::size_t>(square)];
    const std::size_t index = static_cast<std::size_t>(
        ((occupied & mask) * kBishopMagics[static_cast<std::size_t>(square)]) >>
        kBishopShifts[static_cast<std::size_t>(square)]);
    return tables.bishop_attacks[static_cast<std::size_t>(square)]
                                [index];
}

Bitboard rook_attacks(int square, Bitboard occupied) {
    const SlidingAttackTables& tables = sliding_attack_tables();
    const Bitboard mask = tables.rook_masks[static_cast<std::size_t>(square)];
    const std::size_t index = static_cast<std::size_t>(
        ((occupied & mask) * kRookMagics[static_cast<std::size_t>(square)]) >>
        kRookShifts[static_cast<std::size_t>(square)]);
    return tables.rook_attacks[static_cast<std::size_t>(square)]
                              [index];
}

Bitboard queen_attacks(int square, Bitboard occupied) {
    return bishop_attacks(square, occupied) | rook_attacks(square, occupied);
}

int find_king_square(const ChessBoard& board, Color color) {
    const Piece king = color == WHITE ? W_KING : B_KING;
    const Bitboard king_bb = board.piece_bitboard(king);
    if (king_bb == 0) {
        return -1;
    }
    return __builtin_ctzll(king_bb);
}
