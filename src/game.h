// HexTacToe rules: two players take turns colouring hexes on an infinite
// grid. Blue opens with a single hex; after that every turn colours two.
// The first player to get six (or more) in a straight line wins.
#pragma once

#include "hex.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

enum class Player : std::uint8_t { None = 0, Blue = 1, Red = 2 };

inline Player opponent(Player p) {
    return p == Player::Blue ? Player::Red : Player::Blue;
}

const char* playerName(Player p);

class Game {
public:
    static constexpr int kWinLength = 6;
    using Board = std::unordered_map<Hex, Player, HexHash>;

    void reset();

    Player at(Hex h) const;
    bool isEmpty(Hex h) const { return at(h) == Player::None; }

    // The turn structure is derived from the number of tiles placed:
    // tile 0 is Blue's opening turn, tiles 1-2 are Red's, 3-4 Blue's, ...
    static int turnOfTile(int tileIndex) { return (tileIndex + 1) / 2; }
    static Player playerOfTurn(int turn) { return turn % 2 == 0 ? Player::Blue : Player::Red; }

    int tilesPlaced() const { return int(history_.size()); }
    int turnNumber() const { return turnOfTile(tilesPlaced()); }  // 0-based
    Player toMove() const { return playerOfTurn(turnNumber()); }
    int tilesLeftThisTurn() const;  // 1 or 2
    int tilesPerTurn(int turn) const { return turn == 0 ? 1 : 2; }

    // Colours `h` for the player to move. Fails if occupied or the game is over.
    bool place(Hex h);
    // Removes the most recent tile, reverting a win if there was one.
    bool undo();

    bool isOver() const { return winner_ != Player::None; }
    Player winner() const { return winner_; }
    // The cells of the winning line, ordered from one end to the other.
    const std::vector<Hex>& winningLine() const { return winLine_; }

    const std::vector<Hex>& history() const { return history_; }
    const Board& board() const { return board_; }
    // Tiles placed during the given turn (empty if it has not happened yet).
    std::vector<Hex> tilesOfTurn(int turn) const;

private:
    std::vector<Hex> lineThrough(Hex h) const;

    Board board_;
    std::vector<Hex> history_;
    Player winner_ = Player::None;
    std::vector<Hex> winLine_;
};
