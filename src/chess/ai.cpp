#include "chess/ai.hpp"

#include <cctype>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace chess {
namespace {

constexpr int kSearchDepth = 3;
constexpr int kMateScore = 100000;
constexpr int kInfScore = 1000000;

struct EvalRule {
  virtual ~EvalRule() = default;
  virtual double eval(const Position& pos) const = 0;
};

class EvalFuncRule final : public EvalRule {
 public:
  using Callback = std::function<double(const Position&)>;

  explicit EvalFuncRule(Callback callback) : callback_(std::move(callback)) {}

  double eval(const Position& pos) const override { return callback_(pos); }

 private:
  Callback callback_;
};

struct WeightedRule {
  const EvalRule* rule = nullptr;
  double coeff = 1.0;
};

class MasterRule final : public EvalRule {
 public:
  explicit MasterRule(std::vector<WeightedRule> rules) : rules_(std::move(rules)) {}

  double eval(const Position& pos) const override {
    double result = 0.0;
    for (const auto& weighted : rules_) {
      if (!weighted.rule) continue;
      result += weighted.rule->eval(pos) * weighted.coeff;
    }
    return result;
  }

 private:
  std::vector<WeightedRule> rules_;
};

int piece_value(char piece) {
  switch (std::tolower(piece)) {
    case 'p':
      return 1;
    case 'n':
      return 3;
    case 'b':
      return 3;
    case 'r':
      return 5;
    case 'q':
      return 9;
    default:
      return 0;
  }
}

const EvalFuncRule kMaterialRule{[](const Position& pos) {
  int score = 0;
  for (int r = 0; r < kBoardSize; ++r) {
    for (int c = 0; c < kBoardSize; ++c) {
      char piece = pos.board[r][c];
      if (piece == '.') continue;
      const int value = piece_value(piece);
      score += is_white(piece) ? value : -value;
    }
  }
  return static_cast<double>(score);
}};

const MasterRule kEvaluator{{WeightedRule{&kMaterialRule, 1.0}}};

int evaluate(const Position& pos) {
  return static_cast<int>(std::lround(kEvaluator.eval(pos)));
}

int negamax(const Position& pos, int depth, int alpha, int beta) {
  if (depth <= 0) {
    const int eval = evaluate(pos);
    return pos.white_to_move ? eval : -eval;
  }

  const auto moves = generate_legal_moves(pos);
  if (moves.empty()) {
    if (in_check(pos, pos.white_to_move)) return -kMateScore + depth;
    return 0;
  }

  int best = -kInfScore;
  for (const auto& move : moves) {
    Position next = pos;
    apply_move(next, move);
    const int score = -negamax(next, depth - 1, -beta, -alpha);
    if (score > best) best = score;
    if (score > alpha) alpha = score;
    if (alpha >= beta) break;
  }
  return best;
}

}  // namespace

std::optional<Move> find_best_move(const Position& pos) {
  const auto moves = generate_legal_moves(pos);
  if (moves.empty()) return std::nullopt;

  int best = -kInfScore;
  std::optional<Move> best_move;
  int alpha = -kInfScore;
  const int beta = kInfScore;

  for (const auto& move : moves) {
    Position next = pos;
    apply_move(next, move);
    const int score = -negamax(next, kSearchDepth - 1, -beta, -alpha);
    if (score > best || !best_move) {
      best = score;
      best_move = move;
    }
    if (score > alpha) alpha = score;
  }

  return best_move;
}

}  // namespace chess
