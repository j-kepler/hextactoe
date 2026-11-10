#include "game.h"

#include <algorithm>

const char* playerName(Player p) {
    switch (p) {
        case Player::Blue: return "Blue";
        case Player::Red: return "Red";
        default: return "Nobody";
    }
}

void Game::reset() {
    board_.clear();
    history_.clear();
    winner_ = Player::None;
    winLine_.clear();
}

Player Game::at(Hex h) const {
    const auto it = board_.find(h);
    return it == board_.end() ? Player::None : it->second;
}

int Game::tilesLeftThisTurn() const {
    const int n = tilesPlaced();
    if (n == 0) return 1;           // Blue's single opening tile
    return n % 2 == 1 ? 2 : 1;      // turns start on odd tile indices
}

bool Game::place(Hex h) {
    if (isOver() || !isEmpty(h)) return false;
    const Player p = toMove();
    board_[h] = p;
    history_.push_back(h);
    std::vector<Hex> line = lineThrough(h);
    if (int(line.size()) >= kWinLength) {
        winner_ = p;
        winLine_ = std::move(line);
    }
    return true;
}

bool Game::undo() {
    if (history_.empty()) return false;
    board_.erase(history_.back());
    history_.pop_back();
    // A win always comes from the final tile, so removing any tile ends it.
    winner_ = Player::None;
    winLine_.clear();
    return true;
}

std::vector<Hex> Game::tilesOfTurn(int turn) const {
    std::vector<Hex> out;
    if (turn < 0) return out;
    const int first = turn == 0 ? 0 : 2 * turn - 1;
    const int last = std::min(first + tilesPerTurn(turn), tilesPlaced());
    for (int i = first; i < last; ++i) out.push_back(history_[i]);
    return out;
}

std::vector<Hex> Game::lineThrough(Hex h) const {
    const Player p = at(h);
    std::vector<Hex> best;
    for (const Hex axis : kAxes) {
        Hex start = h;
        while (at(start - axis) == p) start = start - axis;
        std::vector<Hex> line;
        for (Hex c = start; at(c) == p; c = c + axis) line.push_back(c);
        if (line.size() > best.size()) best = std::move(line);
    }
    return best;
}
