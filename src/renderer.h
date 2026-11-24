#ifndef FREECELL_RENDERER_H_
#define FREECELL_RENDERER_H_

#include <raylib.h>

#include <optional>
#include <string>

#include "effects.h"
#include "freecell.h"
#include "layout.h"
#include "storage.h"
#include "textures.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Read-only snapshot of UI state handed from Game to Renderer, so
/// drawing stays completely `const` and side-effect-free.
struct FrameView {
  /// @brief Animation clock in seconds.
  float now = 0.0f;
  /// @brief Mouse position, for hover highlights on buttons.
  Vector2 mouse{0.0f, 0.0f};
  /// @brief Chosen card back design.
  int card_back = 0;
  /// @brief True if sound is off.
  bool muted = false;
  /// @brief Seconds on the game clock.
  float seconds = 0.0f;
  /// @brief Saved records.
  SaveData record{};
  /// @brief The hint being shown, if any.
  std::optional<Move> hint;
  /// @brief Animation clock time when the hint appeared.
  float hint_start = 0.0f;
  /// @brief True while the solver is working on a hint.
  bool thinking = false;
  /// @brief The run being dragged (its pile and first card), if any.
  std::optional<PileHit> dragging;
  /// @brief Where the dragged run would land if dropped now, if anywhere.
  std::optional<PileId> drop_target;
  /// @brief True once the game is won.
  bool won = false;
  /// @brief Animation clock time of the win.
  float won_at = 0.0f;
  /// @brief True if the win set a new best time.
  bool new_best = false;
  /// @brief Message shown in a pill at the bottom (empty = none).
  std::string message;
  /// @brief Opacity of the message, so it can fade out.
  float message_alpha = 0.0f;
  /// @brief True while the "choose a deal" box is open.
  bool choosing_deal = false;
  /// @brief Digits typed into the "choose a deal" box so far.
  std::string deal_input;
};

/// @brief Stateless drawing subsystem: turns the table, its animations, and
/// a FrameView into one frame of raylib draw calls.
class Renderer {
 public:
  /// @brief Draws the whole window. Call between `BeginDrawing()` and
  /// `EndDrawing()`.
  /// @param game The rules and the cards on the table.
  /// @param animator Where each card is drawn right now.
  /// @param celebration The win celebration, if running.
  /// @param textures Card images.
  /// @param view UI state for this frame.
  void Draw(const FreeCell& game, const CardAnimator& animator,
            const Celebration& celebration, const CardTextures& textures,
            const FrameView& view) const;
};

}  // namespace freecell

#endif  // FREECELL_RENDERER_H_
