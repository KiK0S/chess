#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chess/ai.hpp"
#include "chess/game.hpp"
#include "engine/app.h"
#include "engine/drivers.h"
#include "engine/input.h"
#include "engine/platform.h"
#include "engine/resource_ids.h"
#include "engine/ui_stream.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include "engine/platform_emscripten.h"
#else
#include <chrono>
#include <thread>
#include "engine/platform_sdl.h"
#endif

namespace {

constexpr float kKeepAliveSeconds = 0.6f;
constexpr float kPi = 3.14159265f;
constexpr float kBoardFlipSeconds = 0.45f;

using namespace chess;

struct BoardGeom {
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float board_size = 0.0f;
  float square = 0.0f;
};

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct MoveAnim {
  bool active = false;
  Move move{};
  char piece = '.';
  bool white = true;
  float elapsed = 0.0f;
  float duration = 0.18f;
  bool capture = false;
  int capture_r = -1;
  int capture_c = -1;
};

struct PromotionState {
  bool active = false;
  Move base_move{};
  bool white = true;
};

struct BoardSpin {
  bool active = false;
  float start = 0.0f;
  float target = 0.0f;
  float elapsed = 0.0f;
  float duration = kBoardFlipSeconds;
};

struct MenuLayout {
  Rect panel{};
  Rect white_toggle{};
  Rect black_toggle{};
  Rect start_button{};
};

bool point_in(const Rect& rect, float x, float y) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

BoardGeom compute_board_geom(int view_w, int view_h) {
  const float padding = 32.0f;
  const float size =
      std::min(static_cast<float>(view_w), static_cast<float>(view_h)) - padding * 2.0f;
  BoardGeom geom{};
  geom.board_size = size;
  geom.square = size / kBoardSize;
  geom.origin_x = (view_w - size) * 0.5f;
  geom.origin_y = (view_h - size) * 0.5f;
  return geom;
}

bool screen_to_board(const BoardGeom& geom, float x, float y, int& out_r, int& out_c) {
  const float rel_x = x - geom.origin_x;
  const float rel_y = y - geom.origin_y;
  if (rel_x < 0.0f || rel_y < 0.0f || rel_x >= geom.board_size || rel_y >= geom.board_size) {
    return false;
  }
  out_c = static_cast<int>(rel_x / geom.square);
  out_r = static_cast<int>(rel_y / geom.square);
  return in_bounds(out_r, out_c);
}

void board_to_screen(const BoardGeom& geom, int r, int c, float& out_x, float& out_y) {
  out_x = geom.origin_x + c * geom.square;
  out_y = geom.origin_y + r * geom.square;
}

void rotate_point(float& x, float& y, float ox, float oy, float radians) {
  const float tx = x - ox;
  const float ty = y - oy;
  const float s = std::sin(radians);
  const float c = std::cos(radians);
  const float rx = tx * c - ty * s;
  const float ry = tx * s + ty * c;
  x = rx + ox;
  y = ry + oy;
}

float smoothstep01(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

struct PromoOption {
  PieceType type = PieceType::Queen;
  float x = 0.0f;
  float y = 0.0f;
  float size = 0.0f;
};

std::array<PromoOption, 4> promotion_options(const BoardGeom& geom, const Move& base_move,
                                             bool white) {
  std::array<PromoOption, 4> options{};
  const PieceType order[] = {
      PieceType::Queen,
      PieceType::Rook,
      PieceType::Bishop,
      PieceType::Knight,
  };
  const float size = geom.square * 0.9f;
  const float gap = geom.square * 0.05f;
  float base_x = geom.origin_x + base_move.to_c * geom.square + (geom.square - size) * 0.5f;
  float start_y = 0.0f;
  if (white) {
    start_y = geom.origin_y + gap;
  } else {
    start_y = geom.origin_y + geom.board_size - (size * 4 + gap * 3) - gap;
  }
  for (int i = 0; i < 4; ++i) {
    options[i].type = order[i];
    options[i].x = base_x;
    options[i].y = start_y + i * (size + gap);
    options[i].size = size;
  }
  return options;
}

enum class ScreenState {
  Menu,
  Playing,
};

class ChessApp : public engine::IApp {
 public:
  explicit ChessApp(int view_w, int view_h)
      : view_w_(view_w), view_h_(view_h), position_(initial_position()) {}

  void init() override {
    register_piece_textures();
  }

  void update(const engine::AppContext& ctx, std::span<const engine::InputEvent> events,
              engine::Frame& out_frame) override {
    geom_ = compute_board_geom(view_w_, view_h_);
    board_center_x_ = geom_.origin_x + geom_.board_size * 0.5f;
    board_center_y_ = geom_.origin_y + geom_.board_size * 0.5f;
    menu_button_ = compute_menu_button();

    if (screen_ == ScreenState::Menu) {
      menu_layout_ = compute_menu_layout();
      handle_menu_input(events);
      draw_menu(out_frame.ui);
      menu_needs_draw_ = false;
      return;
    }

    handle_input(events);
    update_animations(ctx);
    update_ai(ctx);
    draw(out_frame.ui);
  }

  bool wants_continuous_update() const {
    if (screen_ == ScreenState::Menu) return menu_needs_draw_;
    return dragging_ || anim_.active || promotion_.active || ai_pending_ || ai_waiting_ ||
           board_spin_.active;
  }

 private:
  void register_piece_textures() {
    const char* white_names[6] = {
        "chess/opengameart_chess_pieces/chess_piece_2_white_pawn.png",
        "chess/opengameart_chess_pieces/chess_piece_2_white_knight.png",
        "chess/opengameart_chess_pieces/chess_piece_2_white_bishop.png",
        "chess/opengameart_chess_pieces/chess_piece_2_white_rook.png",
        "chess/opengameart_chess_pieces/chess_piece_2_white_queen.png",
        "chess/opengameart_chess_pieces/chess_piece_2_white_king.png",
    };
    const char* black_names[6] = {
        "chess/opengameart_chess_pieces/chess_piece_2_black_pawn.png",
        "chess/opengameart_chess_pieces/chess_piece_2_black_knight.png",
        "chess/opengameart_chess_pieces/chess_piece_2_black_bishop.png",
        "chess/opengameart_chess_pieces/chess_piece_2_black_rook.png",
        "chess/opengameart_chess_pieces/chess_piece_2_black_queen.png",
        "chess/opengameart_chess_pieces/chess_piece_2_black_king.png",
    };
    for (int i = 0; i < 6; ++i) {
      textures_[0][i] = engine::resources::register_texture(white_names[i]);
      textures_[1][i] = engine::resources::register_texture(black_names[i]);
    }
  }

  int piece_index(char piece) const {
    switch (std::tolower(piece)) {
      case 'p':
        return 0;
      case 'n':
        return 1;
      case 'b':
        return 2;
      case 'r':
        return 3;
      case 'q':
        return 4;
      case 'k':
        return 5;
      default:
        return -1;
    }
  }

  engine::TextureId texture_for_piece(char piece) const {
    const int idx = piece_index(piece);
    if (idx < 0) return engine::kInvalidTextureId;
    const int color = is_white(piece) ? 0 : 1;
    return textures_[color][idx];
  }

  void to_board_space(float& x, float& y) const {
    if (board_angle_ == 0.0f) return;
    rotate_point(x, y, board_center_x_, board_center_y_, -board_angle_);
  }

  bool screen_to_board_rotated(float x, float y, int& out_r, int& out_c) const {
    to_board_space(x, y);
    return screen_to_board(geom_, x, y, out_r, out_c);
  }

  void add_board_rect(engine::UIStream& ui, float x, float y, float w, float h,
                      const engine::UIColor& color, bool filled = true,
                      float stroke_width = 1.0f) const {
    engine::add_rect(ui, x, y, w, h, color, filled, stroke_width);
    auto& cmd = ui.commands.back();
    cmd.rotation = board_angle_;
    cmd.origin_x = board_center_x_;
    cmd.origin_y = board_center_y_;
  }

  void add_board_image(engine::UIStream& ui, engine::TextureId tex, float x, float y, float w,
                       float h, const engine::UIColor& color = engine::UIColor{}) const {
    engine::add_image(ui, tex, x, y, w, h, color);
    auto& cmd = ui.commands.back();
    cmd.rotation = board_angle_;
    cmd.origin_x = board_center_x_;
    cmd.origin_y = board_center_y_;
  }

  void add_board_text(engine::UIStream& ui, std::string text, float x, float y, float size,
                      const engine::UIColor& color) const {
    engine::add_text(ui, std::move(text), x, y, size, color);
    auto& cmd = ui.commands.back();
    cmd.rotation = board_angle_;
    cmd.origin_x = board_center_x_;
    cmd.origin_y = board_center_y_;
  }

  Rect compute_menu_button() const {
    Rect rect{};
    rect.w = 120.0f;
    rect.h = 34.0f;
    rect.x = view_w_ - rect.w - 24.0f;
    rect.y = 20.0f;
    return rect;
  }

  MenuLayout compute_menu_layout() const {
    MenuLayout layout{};
    layout.panel.w = 420.0f;
    layout.panel.h = 300.0f;
    layout.panel.x = (view_w_ - layout.panel.w) * 0.5f;
    layout.panel.y = (view_h_ - layout.panel.h) * 0.5f;

    const float pad = 24.0f;
    const float button_h = 48.0f;
    float y = layout.panel.y + 80.0f;

    layout.white_toggle = {layout.panel.x + pad, y, layout.panel.w - pad * 2.0f, button_h};
    y += button_h + 18.0f;
    layout.black_toggle = {layout.panel.x + pad, y, layout.panel.w - pad * 2.0f, button_h};
    y += button_h + 28.0f;
    layout.start_button = {layout.panel.x + pad, y, layout.panel.w - pad * 2.0f, button_h};
    return layout;
  }

  void handle_input(std::span<const engine::InputEvent> events) {
    if (screen_ != ScreenState::Playing) return;

    if (!events.empty()) {
      debug_total_events_ += static_cast<int>(events.size());
    }
    for (const auto& evt : events) {
      debug_last_event_kind_ = evt.kind;
      debug_last_pointer_id_ = evt.pointer_id;
      debug_last_x_ = static_cast<float>(evt.x);
      debug_last_y_ = static_cast<float>(evt.y);
      debug_last_square_r_ = -1;
      debug_last_square_c_ = -1;
      screen_to_board_rotated(debug_last_x_, debug_last_y_, debug_last_square_r_,
                              debug_last_square_c_);

      if (evt.kind == engine::InputKind::PointerDown &&
          point_in(menu_button_, static_cast<float>(evt.x), static_cast<float>(evt.y))) {
        enter_menu();
        return;
      }

      if (game_over_) continue;
      if (board_spin_.active) continue;

      if (promotion_.active) {
        if (evt.kind == engine::InputKind::PointerDown) {
          handle_promotion_click(evt.x, evt.y);
        }
        continue;
      }

      if (anim_.active) continue;

      switch (evt.kind) {
        case engine::InputKind::PointerDown:
          on_pointer_down(evt);
          break;
        case engine::InputKind::PointerMove:
          on_pointer_move(evt);
          break;
        case engine::InputKind::PointerUp:
          on_pointer_up(evt);
          break;
        default:
          break;
      }
    }
  }

  void handle_menu_input(std::span<const engine::InputEvent> events) {
    for (const auto& evt : events) {
      if (evt.kind != engine::InputKind::PointerDown) continue;
      const float x = static_cast<float>(evt.x);
      const float y = static_cast<float>(evt.y);
      if (point_in(menu_layout_.white_toggle, x, y)) {
        white_is_human_ = !white_is_human_;
        continue;
      }
      if (point_in(menu_layout_.black_toggle, x, y)) {
        black_is_human_ = !black_is_human_;
        continue;
      }
      if (point_in(menu_layout_.start_button, x, y)) {
        start_game();
        return;
      }
    }
  }

  void start_game() {
    position_ = initial_position();
    game_over_ = false;
    game_result_.clear();
    reset_transient_state();
    board_angle_ = position_.white_to_move ? 0.0f : kPi;
    screen_ = ScreenState::Playing;
    menu_needs_draw_ = false;
  }

  void enter_menu() {
    reset_transient_state();
    screen_ = ScreenState::Menu;
    menu_needs_draw_ = true;
  }

  void reset_transient_state() {
    dragging_ = false;
    drag_pointer_id_ = -1;
    drag_from_r_ = -1;
    drag_from_c_ = -1;
    drag_moves_.clear();
    promotion_.active = false;
    anim_.active = false;
    board_spin_.active = false;
    ai_pending_ = false;
    ai_waiting_ = false;
    ai_move_.reset();
    ai_delay_ = 0.0f;
  }

  void start_board_spin() {
    const float desired = position_.white_to_move ? 0.0f : kPi;
    if (std::fabs(desired - board_angle_) < 0.001f) return;
    board_spin_.active = true;
    board_spin_.start = board_angle_;
    board_spin_.target = desired;
    board_spin_.elapsed = 0.0f;
    board_spin_.duration = kBoardFlipSeconds;
  }

  void on_pointer_down(const engine::InputEvent& evt) {
    if (!human_turn()) return;
    int r = -1;
    int c = -1;
    if (!screen_to_board_rotated(static_cast<float>(evt.x), static_cast<float>(evt.y), r, c)) {
      return;
    }
    char piece = position_.board[r][c];
    if (piece == '.' || (position_.white_to_move != is_white(piece))) return;
    dragging_ = true;
    drag_pointer_id_ = evt.pointer_id;
    drag_from_r_ = r;
    drag_from_c_ = c;
    drag_x_ = static_cast<float>(evt.x);
    drag_y_ = static_cast<float>(evt.y);
    drag_moves_.clear();
    const auto legal = generate_legal_moves(position_);
    for (const auto& m : legal) {
      if (m.from_r == r && m.from_c == c) {
        drag_moves_.push_back(m);
      }
    }
  }

  void on_pointer_move(const engine::InputEvent& evt) {
    if (!dragging_ || evt.pointer_id != drag_pointer_id_) return;
    drag_x_ = static_cast<float>(evt.x);
    drag_y_ = static_cast<float>(evt.y);
  }

  void on_pointer_up(const engine::InputEvent& evt) {
    if (!dragging_ || evt.pointer_id != drag_pointer_id_) return;
    dragging_ = false;
    int r = -1;
    int c = -1;
    if (!screen_to_board_rotated(static_cast<float>(evt.x), static_cast<float>(evt.y), r, c)) {
      drag_moves_.clear();
      return;
    }
    std::optional<Move> chosen;
    for (const auto& m : drag_moves_) {
      if (m.to_r == r && m.to_c == c) {
        chosen = m;
        break;
      }
    }
    drag_moves_.clear();
    if (!chosen) return;

    if (chosen->promotion != PieceType::None) {
      promotion_.active = true;
      promotion_.base_move = *chosen;
      promotion_.white = position_.white_to_move;
      return;
    }

    start_animation(*chosen);
  }

  void handle_promotion_click(float x, float y) {
    to_board_space(x, y);
    const auto options = promotion_options(geom_, promotion_.base_move, promotion_.white);
    for (const auto& opt : options) {
      if (x >= opt.x && x <= opt.x + opt.size && y >= opt.y && y <= opt.y + opt.size) {
        Move move = promotion_.base_move;
        move.promotion = opt.type;
        promotion_.active = false;
        start_animation(move);
        return;
      }
    }
  }

  void start_animation(const Move& move) {
    anim_.active = true;
    anim_.move = move;
    anim_.elapsed = 0.0f;
    anim_.piece = position_.board[move.from_r][move.from_c];
    anim_.white = is_white(anim_.piece);
    anim_.capture = move.is_capture;
    anim_.capture_r = move.to_r;
    anim_.capture_c = move.to_c;
    if (move.is_en_passant) {
      anim_.capture_r = move.to_r + (anim_.white ? 1 : -1);
      anim_.capture_c = move.to_c;
    }
  }

  void update_animations(const engine::AppContext& ctx) {
    if (anim_.active) {
      anim_.elapsed += static_cast<float>(ctx.delta_seconds);
      if (anim_.elapsed >= anim_.duration) {
        apply_move(position_, anim_.move);
        anim_.active = false;
        update_game_over();
        ai_pending_ = false;
        ai_waiting_ = false;
        start_board_spin();
      }
    }

    if (board_spin_.active) {
      board_spin_.elapsed += static_cast<float>(ctx.delta_seconds);
      const float t = smoothstep01(board_spin_.elapsed / board_spin_.duration);
      board_angle_ = board_spin_.start + (board_spin_.target - board_spin_.start) * t;
      if (board_spin_.elapsed >= board_spin_.duration) {
        board_angle_ = board_spin_.target;
        board_spin_.active = false;
      }
    }
  }

  void update_game_over() {
    const auto legal = generate_legal_moves(position_);
    if (!legal.empty()) {
      game_over_ = false;
      game_result_.clear();
      return;
    }
    game_over_ = true;
    if (in_check(position_, position_.white_to_move)) {
      game_result_ = position_.white_to_move ? "Black wins" : "White wins";
    } else {
      game_result_ = "Stalemate";
    }
  }

  void update_ai(const engine::AppContext& ctx) {
    if (game_over_) return;
    if (!ai_turn()) return;
    if (promotion_.active || anim_.active || dragging_ || board_spin_.active) return;

    if (!ai_pending_) {
      ai_move_ = find_best_move(position_);
      ai_pending_ = ai_move_.has_value();
      ai_waiting_ = ai_pending_;
      ai_delay_ = 0.0f;
      return;
    }

    if (ai_waiting_) {
      ai_delay_ += static_cast<float>(ctx.delta_seconds);
      if (ai_delay_ >= ai_move_delay_) {
        ai_waiting_ = false;
        if (ai_move_) start_animation(*ai_move_);
      }
    }
  }

  bool human_turn() const {
    return position_.white_to_move ? white_is_human_ : black_is_human_;
  }

  bool ai_turn() const { return !human_turn(); }

  void draw(engine::UIStream& ui) {
    draw_board(ui);
    draw_highlights(ui);
    draw_pieces(ui);
    draw_ui(ui);
  }

  void draw_board(engine::UIStream& ui) {
    const engine::UIColor light{0.9f, 0.88f, 0.84f, 1.0f};
    const engine::UIColor dark{0.46f, 0.36f, 0.28f, 1.0f};
    for (int r = 0; r < kBoardSize; ++r) {
      for (int c = 0; c < kBoardSize; ++c) {
        float x = 0.0f;
        float y = 0.0f;
        board_to_screen(geom_, r, c, x, y);
        bool is_light = (r + c) % 2 == 0;
        add_board_rect(ui, x, y, geom_.square, geom_.square, is_light ? light : dark, true);
      }
    }
  }

  void draw_highlights(engine::UIStream& ui) {
    if (dragging_ && !drag_moves_.empty()) {
      const engine::UIColor target_color{0.2f, 0.6f, 0.3f, 0.35f};
      const engine::UIColor capture_color{0.7f, 0.2f, 0.2f, 0.35f};
      for (const auto& m : drag_moves_) {
        float x = 0.0f;
        float y = 0.0f;
        board_to_screen(geom_, m.to_r, m.to_c, x, y);
        add_board_rect(ui, x, y, geom_.square, geom_.square,
                       m.is_capture ? capture_color : target_color, true);
      }
      float x = 0.0f;
      float y = 0.0f;
      board_to_screen(geom_, drag_from_r_, drag_from_c_, x, y);
      add_board_rect(ui, x, y, geom_.square, geom_.square,
                     engine::UIColor{0.15f, 0.45f, 0.8f, 0.35f}, true);
    }
  }

  void draw_pieces(engine::UIStream& ui) {
    const float size = geom_.square * 0.9f;
    const float pad = (geom_.square - size) * 0.5f;

    for (int r = 0; r < kBoardSize; ++r) {
      for (int c = 0; c < kBoardSize; ++c) {
        if (anim_.active) {
          if (r == anim_.move.from_r && c == anim_.move.from_c) continue;
          if (anim_.capture && r == anim_.capture_r && c == anim_.capture_c) continue;
        }
        char piece = position_.board[r][c];
        if (piece == '.') continue;
        engine::TextureId tex = texture_for_piece(piece);
        float x = 0.0f;
        float y = 0.0f;
        board_to_screen(geom_, r, c, x, y);
        add_board_image(ui, tex, x + pad, y + pad, size, size);
      }
    }

    if (anim_.active) {
      float from_x = 0.0f;
      float from_y = 0.0f;
      float to_x = 0.0f;
      float to_y = 0.0f;
      board_to_screen(geom_, anim_.move.from_r, anim_.move.from_c, from_x, from_y);
      board_to_screen(geom_, anim_.move.to_r, anim_.move.to_c, to_x, to_y);
      const float t = std::min(anim_.elapsed / anim_.duration, 1.0f);
      const float sx = from_x + (to_x - from_x) * t;
      const float sy = from_y + (to_y - from_y) * t;
      engine::TextureId tex = texture_for_piece(anim_.piece);
      add_board_image(ui, tex, sx + pad, sy + pad, size, size);
    }

    if (dragging_) {
      char piece = position_.board[drag_from_r_][drag_from_c_];
      if (piece != '.') {
        engine::TextureId tex = texture_for_piece(piece);
        float cx = drag_x_;
        float cy = drag_y_;
        to_board_space(cx, cy);
        add_board_image(ui, tex, cx - size * 0.5f, cy - size * 0.5f, size, size,
                        engine::UIColor{1.0f, 1.0f, 1.0f, 0.9f});
      }
    }
  }

  void draw_ui(engine::UIStream& ui) {
    const engine::UIColor text_color{0.12f, 0.12f, 0.12f, 1.0f};
    std::string status = position_.white_to_move ? "White to move" : "Black to move";
    if (game_over_) status = game_result_;
    engine::add_text(ui, status, 24.0f, 20.0f, 22.0f, text_color);

    const engine::UIColor button_fill{0.9f, 0.88f, 0.85f, 1.0f};
    const engine::UIColor button_border{0.2f, 0.2f, 0.2f, 0.6f};
    engine::add_rect(ui, menu_button_.x, menu_button_.y, menu_button_.w, menu_button_.h,
                     button_fill, true);
    engine::add_rect(ui, menu_button_.x, menu_button_.y, menu_button_.w, menu_button_.h,
                     button_border, false, 2.0f);
    engine::add_text(ui, "Menu", menu_button_.x + 30.0f, menu_button_.y + 8.0f, 18.0f,
                     text_color);

    // Debug overlay for web input troubleshooting.
    {
      const engine::UIColor dbg{0.1f, 0.1f, 0.1f, 0.8f};
      std::string kind = "Unknown";
      switch (debug_last_event_kind_) {
        case engine::InputKind::KeyDown:
          kind = "KeyDown";
          break;
        case engine::InputKind::KeyUp:
          kind = "KeyUp";
          break;
        case engine::InputKind::TextInput:
          kind = "TextInput";
          break;
        case engine::InputKind::PointerMove:
          kind = "PointerMove";
          break;
        case engine::InputKind::PointerDown:
          kind = "PointerDown";
          break;
        case engine::InputKind::PointerUp:
          kind = "PointerUp";
          break;
        default:
          break;
      }
      std::string line1 = "events: " + std::to_string(debug_total_events_) + " last: " + kind;
      std::string line2 = "ptr: " + std::to_string(debug_last_pointer_id_) + " x=" +
                          std::to_string(static_cast<int>(debug_last_x_)) + " y=" +
                          std::to_string(static_cast<int>(debug_last_y_));
      std::string line3 = "square: " + std::to_string(debug_last_square_r_) + "," +
                          std::to_string(debug_last_square_c_);
      engine::add_text(ui, line1, 24.0f, view_h_ - 70.0f, 14.0f, dbg);
      engine::add_text(ui, line2, 24.0f, view_h_ - 50.0f, 14.0f, dbg);
      engine::add_text(ui, line3, 24.0f, view_h_ - 30.0f, 14.0f, dbg);
    }

    if (promotion_.active) {
      const auto options = promotion_options(geom_, promotion_.base_move, promotion_.white);
      add_board_text(ui, "Promote", options[0].x, options[0].y - 24.0f, 18.0f, text_color);
      for (const auto& opt : options) {
        const char piece = make_piece(opt.type, promotion_.white);
        engine::TextureId tex = texture_for_piece(piece);
        add_board_rect(ui, opt.x - 4.0f, opt.y - 4.0f, opt.size + 8.0f, opt.size + 8.0f,
                       engine::UIColor{0.1f, 0.1f, 0.1f, 0.2f}, true);
        add_board_image(ui, tex, opt.x, opt.y, opt.size, opt.size);
      }
    }
  }

  void draw_menu(engine::UIStream& ui) const {
    const engine::UIColor bg{0.96f, 0.95f, 0.92f, 1.0f};
    const engine::UIColor panel{0.99f, 0.98f, 0.96f, 1.0f};
    const engine::UIColor border{0.2f, 0.2f, 0.2f, 0.25f};
    const engine::UIColor text{0.12f, 0.12f, 0.12f, 1.0f};
    const engine::UIColor start_fill{0.2f, 0.55f, 0.3f, 1.0f};
    const engine::UIColor start_text{1.0f, 1.0f, 1.0f, 1.0f};
    const engine::UIColor human_fill{0.82f, 0.92f, 0.86f, 1.0f};
    const engine::UIColor ai_fill{0.93f, 0.86f, 0.82f, 1.0f};

    engine::add_rect(ui, 0.0f, 0.0f, static_cast<float>(view_w_), static_cast<float>(view_h_), bg,
                     true);
    engine::add_rect(ui, menu_layout_.panel.x, menu_layout_.panel.y, menu_layout_.panel.w,
                     menu_layout_.panel.h, panel, true);
    engine::add_rect(ui, menu_layout_.panel.x, menu_layout_.panel.y, menu_layout_.panel.w,
                     menu_layout_.panel.h, border, false, 2.0f);
    engine::add_text(ui, "Chess Matchup", menu_layout_.panel.x + 26.0f,
                     menu_layout_.panel.y + 26.0f, 28.0f, text);

    auto draw_toggle = [&](const Rect& rect, const char* label, bool is_human) {
      const engine::UIColor fill = is_human ? human_fill : ai_fill;
      engine::add_rect(ui, rect.x, rect.y, rect.w, rect.h, fill, true);
      engine::add_rect(ui, rect.x, rect.y, rect.w, rect.h, border, false, 2.0f);
      std::string title = std::string(label) + ": " + (is_human ? "Human" : "AI");
      engine::add_text(ui, title, rect.x + 18.0f, rect.y + 12.0f, 20.0f, text);
    };

    draw_toggle(menu_layout_.white_toggle, "White", white_is_human_);
    draw_toggle(menu_layout_.black_toggle, "Black", black_is_human_);

    engine::add_rect(ui, menu_layout_.start_button.x, menu_layout_.start_button.y,
                     menu_layout_.start_button.w, menu_layout_.start_button.h, start_fill, true);
    engine::add_text(ui, "Start Game", menu_layout_.start_button.x + 24.0f,
                     menu_layout_.start_button.y + 12.0f, 20.0f, start_text);
  }

  int view_w_ = 0;
  int view_h_ = 0;
  BoardGeom geom_{};
  Position position_{};
  engine::TextureId textures_[2][6]{};

  ScreenState screen_ = ScreenState::Menu;
  bool menu_needs_draw_ = true;
  MenuLayout menu_layout_{};
  Rect menu_button_{};
  float board_center_x_ = 0.0f;
  float board_center_y_ = 0.0f;
  float board_angle_ = 0.0f;
  BoardSpin board_spin_{};

  bool white_is_human_ = true;
  bool black_is_human_ = false;
  bool ai_pending_ = false;
  bool ai_waiting_ = false;
  float ai_delay_ = 0.0f;
  float ai_move_delay_ = 0.35f;
  std::optional<Move> ai_move_{};

  bool dragging_ = false;
  int drag_pointer_id_ = -1;
  int drag_from_r_ = -1;
  int drag_from_c_ = -1;
  float drag_x_ = 0.0f;
  float drag_y_ = 0.0f;
  std::vector<Move> drag_moves_{};

  MoveAnim anim_{};
  PromotionState promotion_{};

  bool game_over_ = false;
  std::string game_result_{};

  int debug_total_events_ = 0;
  engine::InputKind debug_last_event_kind_ = engine::InputKind::Unknown;
  int debug_last_pointer_id_ = -1;
  float debug_last_x_ = 0.0f;
  float debug_last_y_ = 0.0f;
  int debug_last_square_r_ = -1;
  int debug_last_square_c_ = -1;
};

}  // namespace

#ifdef __EMSCRIPTEN__
struct LoopState {
  engine::IPlatform* platform = nullptr;
  engine::InputQueue* input = nullptr;
  engine::StepDriver* driver = nullptr;
  ChessApp* app = nullptr;
  double active_until = 0.0;
};

void loop_trampoline(void* user_data) {
  auto* state = static_cast<LoopState*>(user_data);
  if (!state || !state->platform || !state->input || !state->driver || !state->app) return;
  state->platform->pump_events(*state->input);
  const engine::AppContext ctx = state->platform->make_context();
  const bool has_events = !state->input->empty();
  if (has_events) {
    state->active_until = ctx.time_seconds + kKeepAliveSeconds;
  }
  const bool active = has_events || state->app->wants_continuous_update() ||
                      ctx.time_seconds < state->active_until;
  if (!active) return;
  state->driver->step(ctx, *state->input);
  state->platform->present();
}

static ChessApp* g_app = nullptr;
static engine::IPlatform* g_platform = nullptr;
static engine::InputQueue* g_input = nullptr;
static engine::StepDriver* g_driver = nullptr;
static std::unique_ptr<engine::IRenderer> g_renderer{};
static LoopState g_loop_state{};
#endif

int main() {
  const int view_w = 860;
  const int view_h = 860;

  ChessApp app{view_w, view_h};
  engine::PlatformConfig config{};
  config.width = view_w;
  config.height = view_h;
  config.title = "Chess";

#ifdef __EMSCRIPTEN__
  config.renderer = engine::RendererKind::Canvas2D;
  g_app = new ChessApp(view_w, view_h);
  g_platform = new engine::EmscriptenPlatform();
  g_input = new engine::InputQueue();
  g_platform->init(config, *g_input);
  g_renderer = g_platform->create_renderer(config);
  g_renderer->set_view_size(view_w, view_h);
  g_renderer->set_clear_color(engine::UIColor{0.98f, 0.97f, 0.96f, 1.0f});
  g_driver = new engine::StepDriver(*g_app, *g_renderer);
  g_app->init();

  g_loop_state.platform = g_platform;
  g_loop_state.input = g_input;
  g_loop_state.driver = g_driver;
  g_loop_state.app = g_app;
  emscripten_set_main_loop_arg(loop_trampoline, &g_loop_state, 0, true);
#else
  config.renderer = engine::RendererKind::WebGL;
  engine::SdlPlatform platform{};

  engine::InputQueue input{};
  platform.init(config, input);
  auto renderer = platform.create_renderer(config);
  renderer->set_view_size(view_w, view_h);
  renderer->set_clear_color(engine::UIColor{0.98f, 0.97f, 0.96f, 1.0f});

  engine::StepDriver driver{app, *renderer};
  app.init();

  double active_until = 0.0;
  while (platform.pump_events(input)) {
    const engine::AppContext ctx = platform.make_context();
    const bool has_events = !input.empty();
    if (has_events) {
      active_until = ctx.time_seconds + kKeepAliveSeconds;
    }
    const bool active = has_events || app.wants_continuous_update() || ctx.time_seconds < active_until;
    if (active) {
      driver.step(ctx, input);
      platform.present();
      continue;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
#endif

  return 0;
}
