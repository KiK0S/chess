#pragma once

#include <array>
#include <vector>

namespace chess {

inline constexpr int kBoardSize = 8;

enum class PieceType {
  None,
  Pawn,
  Knight,
  Bishop,
  Rook,
  Queen,
  King,
};

struct Move {
  int from_r = 0;
  int from_c = 0;
  int to_r = 0;
  int to_c = 0;
  PieceType promotion = PieceType::None;
  bool is_castle = false;
  bool is_en_passant = false;
  bool is_capture = false;
};

struct Position {
  std::array<std::array<char, kBoardSize>, kBoardSize> board{};
  bool white_to_move = true;
  bool white_king_side = true;
  bool white_queen_side = true;
  bool black_king_side = true;
  bool black_queen_side = true;
  int en_passant_r = -1;
  int en_passant_c = -1;
};

bool is_white(char piece);
bool is_black(char piece);
char make_piece(PieceType type, bool white);
PieceType piece_type(char piece);
Position initial_position();
bool in_bounds(int r, int c);
bool square_attacked(const Position& pos, int r, int c, bool by_white);
bool in_check(const Position& pos, bool white);
void apply_move(Position& pos, const Move& move);
std::vector<Move> generate_pseudo_moves(const Position& pos);
std::vector<Move> generate_legal_moves(const Position& pos);

}  // namespace chess
