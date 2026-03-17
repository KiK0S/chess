#include "chess/game.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace chess {

bool is_white(char piece) { return piece >= 'A' && piece <= 'Z'; }
bool is_black(char piece) { return piece >= 'a' && piece <= 'z'; }

char make_piece(PieceType type, bool white) {
  char base = '.';
  switch (type) {
    case PieceType::Pawn:
      base = 'p';
      break;
    case PieceType::Knight:
      base = 'n';
      break;
    case PieceType::Bishop:
      base = 'b';
      break;
    case PieceType::Rook:
      base = 'r';
      break;
    case PieceType::Queen:
      base = 'q';
      break;
    case PieceType::King:
      base = 'k';
      break;
    case PieceType::None:
      base = '.';
      break;
  }
  if (base == '.') return base;
  return white ? static_cast<char>(std::toupper(base)) : base;
}

PieceType piece_type(char piece) {
  switch (std::tolower(piece)) {
    case 'p':
      return PieceType::Pawn;
    case 'n':
      return PieceType::Knight;
    case 'b':
      return PieceType::Bishop;
    case 'r':
      return PieceType::Rook;
    case 'q':
      return PieceType::Queen;
    case 'k':
      return PieceType::King;
    default:
      return PieceType::None;
  }
}

Position initial_position() {
  Position pos{};
  for (auto& row : pos.board) {
    row.fill('.');
  }
  const std::array<char, kBoardSize> back_rank = {'r', 'n', 'b', 'q', 'k', 'b', 'n', 'r'};
  for (int c = 0; c < kBoardSize; ++c) {
    pos.board[0][c] = back_rank[c];
    pos.board[1][c] = 'p';
    pos.board[6][c] = 'P';
    pos.board[7][c] = static_cast<char>(std::toupper(back_rank[c]));
  }
  pos.white_to_move = true;
  pos.white_king_side = true;
  pos.white_queen_side = true;
  pos.black_king_side = true;
  pos.black_queen_side = true;
  pos.en_passant_r = -1;
  pos.en_passant_c = -1;
  return pos;
}

bool in_bounds(int r, int c) { return r >= 0 && r < kBoardSize && c >= 0 && c < kBoardSize; }

bool square_attacked(const Position& pos, int r, int c, bool by_white) {
  const int pawn_dir = by_white ? 1 : -1;
  const int pawn_row = r + pawn_dir;
  if (in_bounds(pawn_row, c - 1)) {
    char p = pos.board[pawn_row][c - 1];
    if (p == (by_white ? 'P' : 'p')) return true;
  }
  if (in_bounds(pawn_row, c + 1)) {
    char p = pos.board[pawn_row][c + 1];
    if (p == (by_white ? 'P' : 'p')) return true;
  }

  const int knight_offsets[8][2] = {
      {1, 2},
      {2, 1},
      {2, -1},
      {1, -2},
      {-1, -2},
      {-2, -1},
      {-2, 1},
      {-1, 2},
  };
  for (const auto& off : knight_offsets) {
    const int nr = r + off[0];
    const int nc = c + off[1];
    if (!in_bounds(nr, nc)) continue;
    char p = pos.board[nr][nc];
    if (p == (by_white ? 'N' : 'n')) return true;
  }

  const int diag_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  for (const auto& dir : diag_dirs) {
    int nr = r + dir[0];
    int nc = c + dir[1];
    while (in_bounds(nr, nc)) {
      char p = pos.board[nr][nc];
      if (p != '.') {
        if (by_white ? (p == 'B' || p == 'Q') : (p == 'b' || p == 'q')) {
          return true;
        }
        break;
      }
      nr += dir[0];
      nc += dir[1];
    }
  }

  const int ortho_dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const auto& dir : ortho_dirs) {
    int nr = r + dir[0];
    int nc = c + dir[1];
    while (in_bounds(nr, nc)) {
      char p = pos.board[nr][nc];
      if (p != '.') {
        if (by_white ? (p == 'R' || p == 'Q') : (p == 'r' || p == 'q')) {
          return true;
        }
        break;
      }
      nr += dir[0];
      nc += dir[1];
    }
  }

  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      if (dr == 0 && dc == 0) continue;
      const int nr = r + dr;
      const int nc = c + dc;
      if (!in_bounds(nr, nc)) continue;
      char p = pos.board[nr][nc];
      if (p == (by_white ? 'K' : 'k')) return true;
    }
  }

  return false;
}

bool in_check(const Position& pos, bool white) {
  const char king = white ? 'K' : 'k';
  int kr = -1;
  int kc = -1;
  for (int r = 0; r < kBoardSize; ++r) {
    for (int c = 0; c < kBoardSize; ++c) {
      if (pos.board[r][c] == king) {
        kr = r;
        kc = c;
        break;
      }
    }
    if (kr >= 0) break;
  }
  if (kr < 0) return false;
  return square_attacked(pos, kr, kc, !white);
}

void apply_move(Position& pos, const Move& move) {
  char piece = pos.board[move.from_r][move.from_c];
  const bool white = is_white(piece);

  pos.en_passant_r = -1;
  pos.en_passant_c = -1;

  if (std::tolower(piece) == 'k') {
    if (white) {
      pos.white_king_side = false;
      pos.white_queen_side = false;
    } else {
      pos.black_king_side = false;
      pos.black_queen_side = false;
    }
  }

  if (std::tolower(piece) == 'r') {
    if (white && move.from_r == 7 && move.from_c == 0) pos.white_queen_side = false;
    if (white && move.from_r == 7 && move.from_c == 7) pos.white_king_side = false;
    if (!white && move.from_r == 0 && move.from_c == 0) pos.black_queen_side = false;
    if (!white && move.from_r == 0 && move.from_c == 7) pos.black_king_side = false;
  }

  if (!move.is_en_passant) {
    char target = pos.board[move.to_r][move.to_c];
    if (std::tolower(target) == 'r') {
      if (move.to_r == 7 && move.to_c == 0) pos.white_queen_side = false;
      if (move.to_r == 7 && move.to_c == 7) pos.white_king_side = false;
      if (move.to_r == 0 && move.to_c == 0) pos.black_queen_side = false;
      if (move.to_r == 0 && move.to_c == 7) pos.black_king_side = false;
    }
  }

  pos.board[move.from_r][move.from_c] = '.';

  if (move.is_en_passant) {
    const int cap_r = move.to_r + (white ? 1 : -1);
    pos.board[cap_r][move.to_c] = '.';
  }

  if (move.is_castle) {
    if (white) {
      if (move.to_c == 6) {
        pos.board[7][5] = 'R';
        pos.board[7][7] = '.';
      } else if (move.to_c == 2) {
        pos.board[7][3] = 'R';
        pos.board[7][0] = '.';
      }
    } else {
      if (move.to_c == 6) {
        pos.board[0][5] = 'r';
        pos.board[0][7] = '.';
      } else if (move.to_c == 2) {
        pos.board[0][3] = 'r';
        pos.board[0][0] = '.';
      }
    }
  }

  char placed = piece;
  if (move.promotion != PieceType::None) {
    placed = make_piece(move.promotion, white);
  }
  pos.board[move.to_r][move.to_c] = placed;

  if (std::tolower(piece) == 'p' && std::abs(move.to_r - move.from_r) == 2) {
    pos.en_passant_r = (move.to_r + move.from_r) / 2;
    pos.en_passant_c = move.from_c;
  }

  pos.white_to_move = !pos.white_to_move;
}

namespace {

void add_promotion_moves(std::vector<Move>& moves, const Move& base) {
  const PieceType promos[] = {PieceType::Queen, PieceType::Rook, PieceType::Bishop,
                              PieceType::Knight};
  for (PieceType p : promos) {
    Move move = base;
    move.promotion = p;
    moves.push_back(move);
  }
}

}  // namespace

std::vector<Move> generate_pseudo_moves(const Position& pos) {
  std::vector<Move> moves;
  const bool white = pos.white_to_move;
  for (int r = 0; r < kBoardSize; ++r) {
    for (int c = 0; c < kBoardSize; ++c) {
      char piece = pos.board[r][c];
      if (piece == '.') continue;
      if (white != is_white(piece)) continue;
      switch (std::tolower(piece)) {
        case 'p': {
          const int dir = white ? -1 : 1;
          const int start_r = white ? 6 : 1;
          const int promo_r = white ? 0 : 7;
          const int one_r = r + dir;
          if (in_bounds(one_r, c) && pos.board[one_r][c] == '.') {
            Move move{r, c, one_r, c};
            if (one_r == promo_r) {
              add_promotion_moves(moves, move);
            } else {
              moves.push_back(move);
            }
            const int two_r = r + 2 * dir;
            if (r == start_r && pos.board[two_r][c] == '.') {
              moves.push_back(Move{r, c, two_r, c});
            }
          }
          for (int dc : {-1, 1}) {
            const int nr = r + dir;
            const int nc = c + dc;
            if (!in_bounds(nr, nc)) continue;
            char target = pos.board[nr][nc];
            if (target != '.' && (white ? is_black(target) : is_white(target))) {
              Move move{r, c, nr, nc};
              move.is_capture = true;
              if (nr == promo_r) {
                add_promotion_moves(moves, move);
              } else {
                moves.push_back(move);
              }
            } else if (pos.en_passant_r == nr && pos.en_passant_c == nc) {
              Move move{r, c, nr, nc};
              move.is_en_passant = true;
              move.is_capture = true;
              moves.push_back(move);
            }
          }
        } break;
        case 'n': {
          const int offsets[8][2] = {
              {1, 2},
              {2, 1},
              {2, -1},
              {1, -2},
              {-1, -2},
              {-2, -1},
              {-2, 1},
              {-1, 2},
          };
          for (const auto& off : offsets) {
            const int nr = r + off[0];
            const int nc = c + off[1];
            if (!in_bounds(nr, nc)) continue;
            char target = pos.board[nr][nc];
            if (target == '.' || (white ? is_black(target) : is_white(target))) {
              Move move{r, c, nr, nc};
              move.is_capture = (target != '.');
              moves.push_back(move);
            }
          }
        } break;
        case 'b':
        case 'r':
        case 'q': {
          std::vector<std::pair<int, int>> dirs;
          if (std::tolower(piece) == 'b' || std::tolower(piece) == 'q') {
            dirs.push_back({1, 1});
            dirs.push_back({1, -1});
            dirs.push_back({-1, 1});
            dirs.push_back({-1, -1});
          }
          if (std::tolower(piece) == 'r' || std::tolower(piece) == 'q') {
            dirs.push_back({1, 0});
            dirs.push_back({-1, 0});
            dirs.push_back({0, 1});
            dirs.push_back({0, -1});
          }
          for (const auto& dir : dirs) {
            int nr = r + dir.first;
            int nc = c + dir.second;
            while (in_bounds(nr, nc)) {
              char target = pos.board[nr][nc];
              if (target == '.') {
                moves.push_back(Move{r, c, nr, nc});
              } else {
                if (white ? is_black(target) : is_white(target)) {
                  Move move{r, c, nr, nc};
                  move.is_capture = true;
                  moves.push_back(move);
                }
                break;
              }
              nr += dir.first;
              nc += dir.second;
            }
          }
        } break;
        case 'k': {
          for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
              if (dr == 0 && dc == 0) continue;
              const int nr = r + dr;
              const int nc = c + dc;
              if (!in_bounds(nr, nc)) continue;
              char target = pos.board[nr][nc];
              if (target == '.' || (white ? is_black(target) : is_white(target))) {
                Move move{r, c, nr, nc};
                move.is_capture = (target != '.');
                moves.push_back(move);
              }
            }
          }
          const int home_r = white ? 7 : 0;
          if (r == home_r && c == 4) {
            if (white ? pos.white_king_side : pos.black_king_side) {
              if (pos.board[home_r][5] == '.' && pos.board[home_r][6] == '.') {
                if (!square_attacked(pos, home_r, 4, !white) &&
                    !square_attacked(pos, home_r, 5, !white) &&
                    !square_attacked(pos, home_r, 6, !white)) {
                  char rook = pos.board[home_r][7];
                  if (rook == (white ? 'R' : 'r')) {
                    Move move{r, c, home_r, 6};
                    move.is_castle = true;
                    moves.push_back(move);
                  }
                }
              }
            }
            if (white ? pos.white_queen_side : pos.black_queen_side) {
              if (pos.board[home_r][1] == '.' && pos.board[home_r][2] == '.' &&
                  pos.board[home_r][3] == '.') {
                if (!square_attacked(pos, home_r, 4, !white) &&
                    !square_attacked(pos, home_r, 3, !white) &&
                    !square_attacked(pos, home_r, 2, !white)) {
                  char rook = pos.board[home_r][0];
                  if (rook == (white ? 'R' : 'r')) {
                    Move move{r, c, home_r, 2};
                    move.is_castle = true;
                    moves.push_back(move);
                  }
                }
              }
            }
          }
        } break;
        default:
          break;
      }
    }
  }
  return moves;
}

std::vector<Move> generate_legal_moves(const Position& pos) {
  std::vector<Move> legal;
  const auto moves = generate_pseudo_moves(pos);
  for (const auto& move : moves) {
    Position next = pos;
    apply_move(next, move);
    if (!in_check(next, !next.white_to_move)) {
      legal.push_back(move);
    }
  }
  return legal;
}

}  // namespace chess
