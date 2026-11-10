// Computer opponent.
#pragma once

#include "game.h"

#include <cstdint>
#include <vector>

// Chooses the remaining tiles of the current turn for the player to move.
// Returns the cells in the order they should be placed. The list holds
// game.tilesLeftThisTurn() cells, or fewer when they already win.
std::vector<Hex> planTurn(const Game& game, std::uint32_t seed);
