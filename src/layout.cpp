#include "layout.h"

#include <algorithm>
#include <array>

#include "screen.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Layout constants internal to `layout.cpp`.
namespace {

/// @brief Space between two columns.
constexpr float kColumnGap = 20.0f;
/// @brief Left edge of column 0, which centres the eight columns.
constexpr float kLeft =
    (static_cast<float>(kScreenWidth) -
     (kColumnCount * kCardWidth + (kColumnCount - 1) * kColumnGap)) /
    2.0f;
/// @brief Top edge of the free cells and foundations.
constexpr float kTopRowY = 96.0f;
/// @brief Top edge of the columns.
constexpr float kColumnsY = kTopRowY + kCardHeight + 26.0f;
/// @brief Vertical step between cards in a column, before squeezing.
constexpr float kStep = 34.0f;
/// @brief Smallest step a squeezed column may use.
constexpr float kMinStep = 12.0f;
/// @brief Space kept free under the longest column.
constexpr float kBottomMargin = 14.0f;

/// @brief Returns the left edge of slot `column` of the 8-wide grid.
/// @param column Grid column (0..7).
/// @return Pixels.
float GridX(int column) {
  return kLeft + static_cast<float>(column) * (kCardWidth + kColumnGap);
}

/// @brief Returns the step between cards in a column, squeezed to fit.
/// @param count Cards in the column.
/// @return Vertical step in pixels.
float StepFor(int count) {
  if (count <= 1) return kStep;
  const float room = static_cast<float>(kScreenHeight) - kBottomMargin -
                     kColumnsY - kCardHeight;
  return std::clamp(room / static_cast<float>(count - 1), kMinStep, kStep);
}

/// @brief Returns the rectangle of the top card of a pile, or its slot
/// when it is empty. Used as the drop area of the pile.
/// @param game Game to read the pile from.
/// @param pile Pile to locate.
/// @return Bounds in pixels.
Rectangle TopRect(const FreeCell& game, PileId pile) {
  const int size = static_cast<int>(game.Pile(pile).size());
  return size == 0 ? SlotRect(pile) : CardRect(game, pile, size - 1);
}

/// @brief Returns the overlapping area of two rectangles.
/// @param a First rectangle.
/// @param b Second rectangle.
/// @return Area in square pixels (0 if they don't touch).
float OverlapArea(Rectangle a, Rectangle b) {
  // `GetCollisionRec` returns the overlapping part of two rectangles.
  const Rectangle overlap = GetCollisionRec(a, b);
  return overlap.width * overlap.height;
}

}  // namespace

Rectangle SlotRect(PileId pile) {
  switch (pile.kind) {
    case PileKind::Cell:
      return {GridX(pile.index), kTopRowY, kCardWidth, kCardHeight};
    case PileKind::Foundation:
      return {GridX(4 + pile.index), kTopRowY, kCardWidth, kCardHeight};
    case PileKind::Column:
      return {GridX(pile.index), kColumnsY, kCardWidth, kCardHeight};
  }
  return {};
}

Rectangle CardRect(const FreeCell& game, PileId pile, int index) {
  Rectangle rect = SlotRect(pile);
  if (pile.kind == PileKind::Column) {
    rect.y += static_cast<float>(index) *
              StepFor(static_cast<int>(game.Pile(pile).size()));
  }
  return rect;
}

std::optional<PileHit> HitTest(const FreeCell& game, Vector2 point) {
  for (int c = 0; c < kColumnCount; ++c) {
    const PileId pile{PileKind::Column, c};
    const int size = static_cast<int>(game.Pile(pile).size());
    if (size == 0) {
      if (CheckCollisionPointRec(point, SlotRect(pile))) {
        return PileHit{pile, -1};
      }
      continue;
    }
    // Check from the top card down: the top card is drawn last, so it is
    // the one the player sees (and means) where cards overlap.
    for (int i = size - 1; i >= 0; --i) {
      if (CheckCollisionPointRec(point, CardRect(game, pile, i))) {
        return PileHit{pile, i};
      }
    }
  }
  for (int c = 0; c < kCellCount; ++c) {
    const PileId pile{PileKind::Cell, c};
    if (CheckCollisionPointRec(point, SlotRect(pile))) {
      return PileHit{pile, static_cast<int>(game.Pile(pile).size()) - 1};
    }
  }
  for (int f = 0; f < kFoundationCount; ++f) {
    const PileId pile{PileKind::Foundation, f};
    if (CheckCollisionPointRec(point, SlotRect(pile))) {
      return PileHit{pile, static_cast<int>(game.Pile(pile).size()) - 1};
    }
  }
  return std::nullopt;
}

std::optional<Move> FindDrop(const FreeCell& game, PileId from, int index,
                             Rectangle dragged) {
  std::optional<Move> best;
  float best_area = 0.0f;
  // Keeps `move` if it is legal and its drop area overlaps more than the
  // best so far. `[&]` captures the locals above by reference.
  auto consider = [&](const Move& move, Rectangle area) {
    if (!game.CanMove(move)) return;
    const float overlap = OverlapArea(dragged, area);
    if (overlap > best_area) {
      best_area = overlap;
      best = move;
    }
  };
  for (int c = 0; c < kCellCount; ++c) {
    const PileId cell{PileKind::Cell, c};
    consider({from, index, cell}, SlotRect(cell));
  }
  const std::vector<Card>& source = game.Pile(from);
  if (index >= 0 && index < static_cast<int>(source.size())) {
    // Every foundation slot accepts the drop, then sends the card to its
    // own suit's foundation.
    const PileId own{
        PileKind::Foundation,
        static_cast<int>(source[static_cast<std::size_t>(index)].suit)};
    for (int f = 0; f < kFoundationCount; ++f) {
      consider({from, index, own}, SlotRect({PileKind::Foundation, f}));
    }
  }
  for (int c = 0; c < kColumnCount; ++c) {
    const PileId column{PileKind::Column, c};
    consider({from, index, column}, TopRect(game, column));
  }
  return best;
}

Rectangle BarRect() {
  return {12.0f, 10.0f, static_cast<float>(kScreenWidth) - 24.0f, 60.0f};
}

Rectangle ButtonRect(Button button) {
  // `constexpr std::array`: the button widths, fixed at compile time.
  constexpr std::array<float, kButtonCount> kWidths{112.0f, 98.0f, 80.0f, 74.0f,
                                                    96.0f,  84.0f, 100.0f};
  constexpr float kGap = 8.0f;
  const Rectangle bar = BarRect();
  float x = bar.x + 12.0f;
  const auto target = static_cast<std::size_t>(button);
  for (std::size_t i = 0; i < target; ++i) x += kWidths[i] + kGap;
  // The settings buttons are set slightly apart from the game buttons.
  if (button >= Button::CardBack) x += 12.0f;
  return {x, bar.y + 10.0f, kWidths[target], 40.0f};
}

Rectangle CounterRect(int slot) {
  const Rectangle bar = BarRect();
  constexpr float kWidth = 124.0f;
  return {bar.x + bar.width - 12.0f - static_cast<float>(3 - slot) * kWidth,
          bar.y + 8.0f, kWidth - 8.0f, 44.0f};
}

}  // namespace freecell
