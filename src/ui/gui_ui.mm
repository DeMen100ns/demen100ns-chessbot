#import <Cocoa/Cocoa.h>

#include "chess/gui_app.h"
#include "chess/bot.h"

#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

namespace {
constexpr CGFloat kPieceScale = 0.88;
constexpr CGFloat kDragPieceScale = 0.94;
constexpr CGFloat kPieceYOffsetFactor = 0.01;
constexpr int kGuiSearchTimeLimitMs = 24 * 60 * 60 * 1000;

enum class PlayerMode {
    Human,
    Computer
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

Piece DefaultPromotion(Color color) {
    return color == WHITE ? W_QUEEN : B_QUEEN;
}

NSString* PieceSymbol(Piece piece) {
    switch (piece) {
        case W_PAWN:
            return @"\u2659";
        case W_KNIGHT:
            return @"\u2658";
        case W_BISHOP:
            return @"\u2657";
        case W_ROOK:
            return @"\u2656";
        case W_QUEEN:
            return @"\u2655";
        case W_KING:
            return @"\u2654";
        case B_PAWN:
            return @"\u265F";
        case B_KNIGHT:
            return @"\u265E";
        case B_BISHOP:
            return @"\u265D";
        case B_ROOK:
            return @"\u265C";
        case B_QUEEN:
            return @"\u265B";
        case B_KING:
            return @"\u265A";
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
        case W_PAWN:
            return @"wp.png";
        case W_KNIGHT:
            return @"wn.png";
        case W_BISHOP:
            return @"wb.png";
        case W_ROOK:
            return @"wr.png";
        case W_QUEEN:
            return @"wq.png";
        case W_KING:
            return @"wk.png";
        case B_PAWN:
            return @"bp.png";
        case B_KNIGHT:
            return @"bn.png";
        case B_BISHOP:
            return @"bb.png";
        case B_ROOK:
            return @"br.png";
        case B_QUEEN:
            return @"bq.png";
        case B_KING:
            return @"bk.png";
        case EMPTY:
        default:
            return nil;
    }
}

NSString* FindPieceAssetPath(Piece piece) {
    NSString* filename = PieceAssetFilename(piece);
    if (filename == nil) {
        return nil;
    }

    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSArray<NSString*>* searchRoots = @[
        [[NSFileManager defaultManager] currentDirectoryPath],
        [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent],
        [NSBundle mainBundle].resourcePath ?: @""
    ];

    for (NSString* root in searchRoots) {
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
    NSArray<NSString*>* searchRoots = @[
        [[NSFileManager defaultManager] currentDirectoryPath],
        [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent],
        [NSBundle mainBundle].resourcePath ?: @""
    ];

    for (NSString* root in searchRoots) {
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

    const NSRect drawRect = NSMakeRect(0, imageSize * 0.01, imageSize, imageSize);
    [symbol drawInRect:drawRect withAttributes:attrs];

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
}  // namespace

@interface ChessBoardView : NSView
- (void)setStatusLabel:(NSTextField*)statusLabel;
- (void)setWhiteModeControl:(NSPopUpButton*)control;
- (void)setBlackModeControl:(NSPopUpButton*)control;
- (void)setDepthControl:(NSPopUpButton*)control;
- (void)startNewGame:(id)sender;
- (void)depthChanged:(id)sender;
@end

@implementation ChessBoardView {
    ChessBoard _board;
    Bot _bot;
    NSInteger _selectedSquare;
    NSTextField* _statusLabel;
    NSPopUpButton* _whiteModeControl;
    NSPopUpButton* _blackModeControl;
    NSPopUpButton* _depthControl;
    PlayerMode _whiteMode;
    PlayerMode _blackMode;
    NSInteger _searchDepth;
    BOOL _computerThinking;
    NSMutableDictionary<NSString*, NSImage*>* _pieceImageCache;
    NSImage* _boardImage;
    std::vector<int> _highlightedSquares;
    NSInteger _dragSourceSquare;
    BOOL _isDraggingPiece;
    NSPoint _dragLocation;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _selectedSquare = -1;
        _whiteMode = PlayerMode::Human;
        _blackMode = PlayerMode::Computer;
        _searchDepth = 3;
        _bot = Bot(static_cast<int>(_searchDepth));
        const char* disableOnlineTb = std::getenv("CHESS_DISABLE_ONLINE_TB");
        if (disableOnlineTb == nullptr || disableOnlineTb[0] == '\0') {
            const char* tablebaseUrl = std::getenv("CHESS_ONLINE_TB_URL");
            const char* timeoutMs = std::getenv("CHESS_ONLINE_TB_TIMEOUT_MS");
            const int requestTimeoutMs = (timeoutMs != nullptr && timeoutMs[0] != '\0')
                ? std::max(1, std::atoi(timeoutMs))
                : 200;
            _bot.enable_online_tablebase(
                tablebaseUrl != nullptr && tablebaseUrl[0] != '\0'
                    ? tablebaseUrl
                    : "https://tablebase.lichess.ovh/standard",
                requestTimeoutMs);
        }
        _computerThinking = NO;
        _pieceImageCache = [[NSMutableDictionary alloc] init];
        _dragSourceSquare = -1;
        _isDraggingPiece = NO;
        _dragLocation = NSZeroPoint;
        NSString* boardPath = FindBoardAssetPath();
        if (boardPath != nil) {
            _boardImage = [[NSImage alloc] initWithContentsOfFile:boardPath];
        }
        [self resetGame];
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)setStatusLabel:(NSTextField*)statusLabel {
    _statusLabel = statusLabel;
    [self updateStatus];
}

- (void)setWhiteModeControl:(NSPopUpButton*)control {
    _whiteModeControl = control;
}

- (void)setBlackModeControl:(NSPopUpButton*)control {
    _blackModeControl = control;
}

- (void)setDepthControl:(NSPopUpButton*)control {
    _depthControl = control;
    if (_depthControl != nil) {
        [_depthControl selectItemWithTitle:[NSString stringWithFormat:@"%ld", static_cast<long>(_searchDepth)]];
    }
}

- (void)depthChanged:(id)sender {
    (void)sender;
    if (_depthControl == nil) {
        return;
    }

    const NSInteger selectedDepth = _depthControl.titleOfSelectedItem.integerValue;
    if (selectedDepth > 0) {
        _searchDepth = selectedDepth;
        _bot.depth = static_cast<int>(_searchDepth);
        [self updateStatus];
    }
}

- (void)startNewGame:(id)sender {
    (void)sender;
    _whiteMode = [_whiteModeControl indexOfSelectedItem] == 0 ? PlayerMode::Human : PlayerMode::Computer;
    _blackMode = [_blackModeControl indexOfSelectedItem] == 0 ? PlayerMode::Human : PlayerMode::Computer;
    [self depthChanged:nil];
    [self resetGame];
}

- (void)resetGame {
    _board.initialize();
    _bot.depth = static_cast<int>(_searchDepth);
    _bot.reset_history();
    _bot.record_position(_board);
    _selectedSquare = -1;
    _computerThinking = NO;
    _dragSourceSquare = -1;
    _isDraggingPiece = NO;
    _highlightedSquares.clear();
    [self updateStatus];
    [self setNeedsDisplay:YES];
    [self maybeRunComputerTurn];
}

- (CGFloat)boardSize {
    return MIN(self.bounds.size.width, self.bounds.size.height);
}

- (CGFloat)cellSize {
    return [self boardSize] / 8.0;
}

- (BOOL)isHumanTurn {
    return (_board.turn == WHITE && _whiteMode == PlayerMode::Human) ||
           (_board.turn == BLACK && _blackMode == PlayerMode::Human);
}

- (BOOL)isComputerTurn {
    return (_board.turn == WHITE && _whiteMode == PlayerMode::Computer) ||
           (_board.turn == BLACK && _blackMode == PlayerMode::Computer);
}

- (void)updateHighlightedSquares {
    _highlightedSquares.clear();
    if (_selectedSquare < 0) {
        return;
    }

    const std::vector<Move> moves = _board.generate_moves(_board.turn);
    for (const Move& move : moves) {
        if (move.from == _selectedSquare) {
            _highlightedSquares.push_back(move.to);
        }
    }
}

- (void)selectSquare:(NSInteger)square {
    _selectedSquare = square;
    [self updateHighlightedSquares];
    [self setNeedsDisplay:YES];
}

- (void)clearSelection {
    _selectedSquare = -1;
    _highlightedSquares.clear();
    [self setNeedsDisplay:YES];
}

- (BOOL)isHighlightedSquare:(NSInteger)square {
    for (int target : _highlightedSquares) {
        if (target == square) {
            return YES;
        }
    }
    return NO;
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

- (void)updateStatus {
    if (_statusLabel == nil) {
        return;
    }

    NSString* status = _board.turn == WHITE ? @"White to move" : @"Black to move";
    if (_board.is_checkmate(_board.turn)) {
        status = _board.turn == WHITE ? @"Checkmate. Black wins." : @"Checkmate. White wins.";
    } else if (_board.is_stalemate(_board.turn)) {
        status = @"Stalemate. Draw.";
    } else if (_computerThinking) {
        status = _board.turn == WHITE ? @"White computer is thinking..." : @"Black computer is thinking...";
    } else if (_board.is_in_check(_board.turn)) {
        status = _board.turn == WHITE ? @"White is in check" : @"Black is in check";
    }

    NSString* decoratedStatus =
        [NSString stringWithFormat:@"Depth %ld\n%@", static_cast<long>(_searchDepth), status];
    [_statusLabel setStringValue:decoratedStatus];
}

- (NSInteger)squareAtPoint:(NSPoint)point {
    const CGFloat size = [self boardSize];
    if (point.x < 0 || point.y < 0 || point.x >= size || point.y >= size) {
        return -1;
    }

    const NSInteger file = static_cast<NSInteger>(point.x / [self cellSize]);
    const NSInteger rankFromTop = static_cast<NSInteger>(point.y / [self cellSize]);
    const NSInteger rank = 7 - rankFromTop;
    return rank * 8 + file;
}

- (BOOL)isSelectablePieceAtSquare:(NSInteger)square {
    if (square < 0 || square >= 64) {
        return NO;
    }
    const Piece piece = _board.piece_at(static_cast<int>(square));
    if (piece == EMPTY) {
        return NO;
    }
    return (_board.turn == WHITE && IsWhitePiece(piece)) ||
           (_board.turn == BLACK && !IsWhitePiece(piece));
}

- (Move)moveFromSquare:(NSInteger)from toSquare:(NSInteger)to {
    const Piece movingPiece = _board.piece_at(static_cast<int>(from));
    if ((movingPiece == W_PAWN && to / 8 == 7) || (movingPiece == B_PAWN && to / 8 == 0)) {
        return Move(static_cast<int>(from), static_cast<int>(to), DefaultPromotion(_board.turn));
    }
    return Move(static_cast<int>(from), static_cast<int>(to));
}

- (void)applyMove:(const Move&)move {
    _board = _board.make_move(move);
    _bot.record_position(_board);
    _selectedSquare = -1;
    _dragSourceSquare = -1;
    _isDraggingPiece = NO;
    _highlightedSquares.clear();
    [self updateStatus];
    [self setNeedsDisplay:YES];
    [self maybeRunComputerTurn];
}

- (void)maybeRunComputerTurn {
    if (_computerThinking || !_board.generate_moves(_board.turn).size()) {
        [self updateStatus];
        return;
    }
    if (![self isComputerTurn]) {
        [self updateStatus];
        return;
    }

    _computerThinking = YES;
    [self updateStatus];
    [self setNeedsDisplay:YES];

    __weak ChessBoardView* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        ChessBoardView* strongSelf = weakSelf;
        if (strongSelf == nil) {
            return;
        }

        const ChessBoard snapshot = strongSelf->_board;
        const Bot bot = strongSelf->_bot;
        Move bestMove = bot.choose_move(snapshot, bot.depth, kGuiSearchTimeLimitMs);

        dispatch_async(dispatch_get_main_queue(), ^{
            ChessBoardView* innerSelf = weakSelf;
            if (innerSelf == nil) {
                return;
            }
            innerSelf->_computerThinking = NO;
            if (innerSelf->_board.turn != snapshot.turn) {
                [innerSelf updateStatus];
                return;
            }
            [innerSelf applyMove:bestMove];
        });
    });
}

- (void)attemptMoveFromSquare:(NSInteger)from toSquare:(NSInteger)to {
    if (from < 0 || to < 0) {
        return;
    }

    const Move move = [self moveFromSquare:from toSquare:to];
    if (_board.valid_move(move, _board.turn)) {
        [self applyMove:move];
        return;
    }

    if ([self isSelectablePieceAtSquare:to]) {
        [self selectSquare:to];
        return;
    }

    NSBeep();
    [self selectSquare:from];
}

- (void)mouseDown:(NSEvent*)event {
    if (_computerThinking || ![self isHumanTurn]) {
        return;
    }
    if (_board.is_checkmate(_board.turn) || _board.is_stalemate(_board.turn)) {
        return;
    }

    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSInteger clickedSquare = [self squareAtPoint:point];
    if (clickedSquare < 0) {
        return;
    }

    if (![self isSelectablePieceAtSquare:clickedSquare]) {
        return;
    }

    _dragSourceSquare = clickedSquare;
    _dragLocation = point;
    _isDraggingPiece = NO;
    [self selectSquare:clickedSquare];
}

- (void)mouseDragged:(NSEvent*)event {
    if (_computerThinking || ![self isHumanTurn] || _dragSourceSquare < 0) {
        return;
    }

    _isDraggingPiece = YES;
    _dragLocation = [self convertPoint:event.locationInWindow fromView:nil];
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
    if (_computerThinking || ![self isHumanTurn] || _dragSourceSquare < 0) {
        return;
    }

    const NSInteger fromSquare = _dragSourceSquare;
    const BOOL wasDragging = _isDraggingPiece;
    _dragSourceSquare = -1;
    _isDraggingPiece = NO;

    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSInteger releasedSquare = [self squareAtPoint:point];

    if (releasedSquare < 0) {
        [self selectSquare:fromSquare];
        return;
    }

    if (wasDragging) {
        if (releasedSquare == fromSquare) {
            [self selectSquare:fromSquare];
            return;
        }
        [self attemptMoveFromSquare:fromSquare toSquare:releasedSquare];
        return;
    }

    if (releasedSquare == fromSquare) {
        [self selectSquare:fromSquare];
        return;
    }

    [self attemptMoveFromSquare:fromSquare toSquare:releasedSquare];
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
    const NSInteger draggingSquare = _isDraggingPiece ? _dragSourceSquare : -1;

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

            if (square == _selectedSquare) {
                [[NSColor colorWithCalibratedRed:0.98 green:0.82 blue:0.18 alpha:0.45] setFill];
                NSBezierPath* selectedPath =
                    [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rect, 4.0, 4.0) xRadius:10.0 yRadius:10.0];
                [selectedPath fill];
            }

            if ([self isHighlightedSquare:square]) {
                const BOOL isCapture = !_board.is_empty(static_cast<int>(square));
                if (isCapture) {
                    [[NSColor colorWithCalibratedRed:0.17 green:0.62 blue:0.34 alpha:0.28] setFill];
                    NSBezierPath* capturePath =
                        [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect, cell * 0.13, cell * 0.13)];
                    [capturePath fill];
                    [[NSColor colorWithCalibratedRed:0.10 green:0.42 blue:0.22 alpha:0.65] setStroke];
                    [capturePath setLineWidth:3.0];
                    [capturePath stroke];
                } else {
                    [[NSColor colorWithCalibratedRed:0.10 green:0.42 blue:0.22 alpha:0.58] setFill];
                    NSBezierPath* dotPath =
                        [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect, cell * 0.34, cell * 0.34)];
                    [dotPath fill];
                }
            }

            const Piece piece = _board.piece_at(static_cast<int>(square));
            if (piece != EMPTY && !(_isDraggingPiece && square == draggingSquare)) {
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

    if (_isDraggingPiece && _selectedSquare >= 0) {
        const Piece draggedPiece = _board.piece_at(static_cast<int>(_selectedSquare));
        if (draggedPiece != EMPTY) {
            NSImage* image = [self imageForPiece:draggedPiece];
            const CGFloat imageSize = cell * kDragPieceScale;
            NSRect dragRect = NSMakeRect(_dragLocation.x - imageSize / 2.0,
                                         _dragLocation.y - imageSize / 2.0 + cell * 0.03,
                                         imageSize,
                                         imageSize);
            [image drawInRect:dragRect
                     fromRect:NSZeroRect
                    operation:NSCompositingOperationSourceOver
                     fraction:0.96
               respectFlipped:YES
                        hints:nil];
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

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow* _window;
    ChessBoardView* _boardView;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    const NSRect frame = NSMakeRect(0, 0, 760, 680);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable |
                                                     NSWindowStyleMaskResizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window center];
    [_window setTitle:@"Chess Bot"];

    NSView* contentView = [_window contentView];

    _boardView = [[ChessBoardView alloc] initWithFrame:NSMakeRect(20, 20, 640, 640)];
    [_boardView setWantsLayer:YES];
    _boardView.layer.backgroundColor = NSColor.whiteColor.CGColor;
    [contentView addSubview:_boardView];

    NSTextField* whiteLabel = [NSTextField labelWithString:@"White"];
    [whiteLabel setFrame:NSMakeRect(675, 590, 60, 24)];
    [contentView addSubview:whiteLabel];

    NSPopUpButton* whiteMode = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(670, 560, 70, 28)];
    [whiteMode addItemsWithTitles:@[@"Human", @"Computer"]];
    [whiteMode selectItemAtIndex:0];
    [contentView addSubview:whiteMode];

    NSTextField* blackLabel = [NSTextField labelWithString:@"Black"];
    [blackLabel setFrame:NSMakeRect(675, 510, 60, 24)];
    [contentView addSubview:blackLabel];

    NSPopUpButton* blackMode = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(670, 480, 70, 28)];
    [blackMode addItemsWithTitles:@[@"Human", @"Computer"]];
    [blackMode selectItemAtIndex:1];
    [contentView addSubview:blackMode];

    NSTextField* depthLabel = [NSTextField labelWithString:@"Depth"];
    [depthLabel setFrame:NSMakeRect(675, 430, 60, 24)];
    [contentView addSubview:depthLabel];

    NSPopUpButton* depthControl = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(670, 400, 70, 28)];
    [depthControl addItemsWithTitles:@[@"1", @"2", @"3", @"4", @"5", @"6", @"7"]];
    [depthControl selectItemWithTitle:@"3"];
    [depthControl setTarget:_boardView];
    [depthControl setAction:@selector(depthChanged:)];
    [contentView addSubview:depthControl];

    NSButton* newGameButton = [[NSButton alloc] initWithFrame:NSMakeRect(670, 350, 74, 32)];
    [newGameButton setTitle:@"New Game"];
    [newGameButton setBezelStyle:NSBezelStyleRounded];
    [newGameButton setTarget:_boardView];
    [newGameButton setAction:@selector(startNewGame:)];
    [contentView addSubview:newGameButton];

    NSTextField* helpLabel = [NSTextField labelWithString:@"Click a piece, then click\na target square."];
    [helpLabel setFrame:NSMakeRect(670, 275, 80, 50)];
    [helpLabel setLineBreakMode:NSLineBreakByWordWrapping];
    [helpLabel setUsesSingleLineMode:NO];
    [contentView addSubview:helpLabel];

    NSTextField* statusLabel = [NSTextField labelWithString:@""];
    [statusLabel setFrame:NSMakeRect(670, 190, 80, 70)];
    [statusLabel setLineBreakMode:NSLineBreakByWordWrapping];
    [statusLabel setUsesSingleLineMode:NO];
    [contentView addSubview:statusLabel];

    [_boardView setStatusLabel:statusLabel];
    [_boardView setWhiteModeControl:whiteMode];
    [_boardView setBlackModeControl:blackMode];
    [_boardView setDepthControl:depthControl];

    [_window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

int RunChessApp() {
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
