#include "bot.h"

#include <cassert>

namespace {
bool same_move(const Move& lhs, const Move& rhs) {
    return lhs.from == rhs.from &&
           lhs.to == rhs.to &&
           lhs.promotion == rhs.promotion;
}
}  // namespace

int main() {
    const ChessBoard board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    const Move expected(36, 43);  // e5d6 en passant

    Bot bot(5);
    const Move untimed = bot.choose_move(board);
    assert(same_move(untimed, expected));

    const Move timed = bot.choose_move_timed(board, 5000);
    assert(same_move(timed, expected));

    return 0;
}
