#pragma once

#include <optional>

#include "chess/game.hpp"

namespace chess {

std::optional<Move> find_best_move(const Position& pos);

}  // namespace chess
