#ifndef FREECELL_LAYOUT_H_
#define FREECELL_LAYOUT_H_

#include <raylib.h>

#include <cstdint>
#include <optional>

#include "freecell.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Card width in pixels.
inline constexpr float kCardWidth = 118.0f;
/// @brief Card height in pixels (the images are 5:7, like a poker card).
inline constexpr float kCardHeight = kCardWidth * 1.4f;
/// @brief Corner roundness that matches the card images, for shadows and
/// outlines (raylib's roundness is the corner radius over half the shorter
/// side).
inline constexpr float kCardRoundness = 0.07f;

/// @brief A card under the mouse, or an empty pile.
struct PileHit {
  /// @brief Pile that was hit.
  PileId pile;
  /// @brief Position of the hit card in that pile, or `-1` for an empty
  /// pile.
  int index = -1;
};

/// @brief Returns the screen rectangle of an empty pile's slot.
/// @param pile Pile to place.
/// @return Slot bounds in pixels.
Rectangle SlotRect(PileId pile);

/// @brief Returns where card `index` of `pile` sits on screen right now.
/// Columns fan downwards and squeeze together when long, so they always
/// fit in the window.
/// @param game Game to read the piles from.
/// @param pile Pile the card is in.
/// @param index Position of the card in the pile.
/// @return Card bounds in pixels.
Rectangle CardRect(const FreeCell& game, PileId pile, int index);

/// @brief Finds the top-most card (or empty pile) under `point`.
/// @param game Game to read the piles from.
/// @param point Screen position, usually the mouse.
/// @return What was hit, or `std::nullopt` for bare table.
std::optional<PileHit> HitTest(const FreeCell& game, Vector2 point);

/// @brief Finds the best legal pile to drop a dragged run on: the one whose
/// top card (or empty slot) overlaps the dragged card the most. A drop on
/// any foundation goes to the dragged card's own suit.
/// @param game Game to check moves against.
/// @param from Pile the run is dragged from.
/// @param index Position of the run's first card in that pile.
/// @param dragged Screen rectangle of the run's first card.
/// @return The move, or `std::nullopt` if no legal pile is under it.
std::optional<Move> FindDrop(const FreeCell& game, PileId from, int index,
                             Rectangle dragged);

/// @brief Top bar across the window.
/// @return Bar bounds in pixels.
Rectangle BarRect();

/// @brief The buttons in the top bar, left to right.
enum class Button : std::uint8_t {
  /// @brief Deal a new random game.
  NewGame = 0,
  /// @brief Start this deal again.
  Restart,
  /// @brief Take back the last move.
  Undo,
  /// @brief Ask the solver for a hint.
  Hint,
  /// @brief Type in a deal number.
  ChooseDeal,
  /// @brief Change the card back design.
  CardBack,
  /// @brief Sound on or off.
  Sound,
};

/// @brief Number of Button values.
inline constexpr int kButtonCount = 7;

/// @brief Returns the screen rectangle of a top-bar button.
/// @param button Button to place.
/// @return Button bounds in pixels.
Rectangle ButtonRect(Button button);

/// @brief Returns the screen rectangle of one of the three counters (deal,
/// moves, time) at the right of the top bar.
/// @param slot `0`, `1`, or `2`, left to right.
/// @return Counter bounds in pixels.
Rectangle CounterRect(int slot);

}  // namespace freecell

#endif  // FREECELL_LAYOUT_H_
