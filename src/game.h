#ifndef FREECELL_GAME_H_
#define FREECELL_GAME_H_

#include <raylib.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "audio.h"
#include "effects.h"
#include "freecell.h"
#include "layout.h"
#include "renderer.h"
#include "solver.h"
#include "storage.h"
#include "textures.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Top-level coordinator: reads input, drives the FreeCell rules,
/// plays the automatic foundation moves, asks the solver for hints on a
/// background thread, keeps the clock, records results, and hands
/// everything to the Renderer.
///
/// `main()` constructs one `Game` inside an explicit RAII scope block so its
/// destructor (which saves progress, stops the solver, and frees textures
/// and sounds) runs before `CloseAudioDevice()` and `CloseWindow()`.
class Game {
 public:
  /// @brief Loads card images, sounds, and saved records, then deals.
  Game();

  /// @brief Stops the solver, records an unfinished game, and saves.
  ~Game();

  /// @brief Non-copyable: there is only ever one game, and a copy would free
  /// the same textures and sounds twice.
  Game(const Game&) = delete;
  /// @brief Non-copy-assignable for the same reason.
  Game& operator=(const Game&) = delete;

  /// @brief Handles input and advances animations by one frame.
  /// @param dt Seconds elapsed since the previous frame.
  void Update(float dt);

  /// @brief Renders the current frame between raylib's `BeginDrawing()` and
  /// `EndDrawing()`. Marked `const` so drawing can never mutate game state.
  void Draw() const;

  /// @brief Returns true once the player pressed Esc to quit.
  /// @return True to end the main loop.
  bool quit_requested() const { return quit_requested_; }

 private:
  /// @brief A run of cards the player is holding with the mouse.
  struct Drag {
    /// @brief Pile the run comes from.
    PileId pile;
    /// @brief Position of the run's first card in that pile.
    int index = 0;
    /// @brief Mouse position relative to the first card's top-left corner,
    /// so the card doesn't jump to put its corner under the pointer.
    Vector2 grab{0.0f, 0.0f};
    /// @brief Where the mouse button went down.
    Vector2 press{0.0f, 0.0f};
    /// @brief True once the mouse has moved far enough to count as a drag
    /// rather than a click.
    bool moved = false;
  };

  /// @brief Deals game `number` with a dealing animation.
  /// @param number Deal number.
  void StartDeal(int number);

  /// @brief Handles clicks and drags on the table and the top bar.
  void HandleMouse();

  /// @brief Handles hotkeys.
  void HandleKeyboard();

  /// @brief Handles typing in the "choose a deal" box.
  void HandleDealInput();

  /// @brief Handles a click on a top-bar button.
  /// @param button The button.
  void PressButton(Button button);

  /// @brief Makes a player move and plays its sound.
  /// @param move The move (checked for legality inside).
  void MakeMove(const Move& move);

  /// @brief Explains why a dragged run could not be dropped, if the reason
  /// is that it is longer than the free space allows.
  /// @param from Pile the run was dragged from.
  /// @param index First card of the run.
  void ExplainFailedDrop(PileId from, int index);

  /// @brief Takes back the last player move.
  void UndoMove();

  /// @brief Starts the solver on a background thread to find a hint.
  void RequestHint();

  /// @brief Picks up the solver's answer once it is ready.
  void PollHint();

  /// @brief Stops any running solve; its answer will be ignored.
  void CancelHint();

  /// @brief Plays every pending automatic foundation move at once.
  void FlushAutoMoves();

  /// @brief Runs after every change to the table: clears the hint, checks
  /// for a win, and warns when no move is left.
  void AfterChange();

  /// @brief Celebrates and records a win.
  void Win();

  /// @brief Tells the animator where every card should be this frame.
  /// @param dt Seconds since the last frame.
  void SyncAnimator(float dt);

  /// @brief Shows a message at the bottom of the window for a while.
  /// @param text Message to show.
  void ShowMessage(std::string text);

  /// @brief Records a game the player is leaving before winning.
  void RecordAbandonedGame();

  /// @brief Writes `save_` to `save_path_` so records persist.
  void PersistSaveData();

  /// @brief Filesystem path where records are stored.
  std::filesystem::path save_path_;
  /// @brief Loaded settings and records.
  SaveData save_{};
  /// @brief Card images.
  CardTextures textures_;
  /// @brief Synthesised sounds.
  Audio audio_;
  /// @brief The rules and the cards on the table.
  FreeCell game_;
  /// @brief Where each card is drawn.
  CardAnimator animator_;
  /// @brief The win celebration.
  Celebration celebration_;
  /// @brief Stateless drawing subsystem.
  Renderer renderer_;

  /// @brief Animation clock in seconds.
  float now_ = 0.0f;
  /// @brief Game clock in seconds.
  float clock_ = 0.0f;
  /// @brief True from the first move until the game is won.
  bool clock_running_ = false;
  /// @brief Animation clock time of the next automatic foundation move.
  float next_auto_move_ = 0.0f;
  /// @brief Counts every change to the table, so a hint that was worked
  /// out for an older table can be recognised and thrown away.
  int table_version_ = 0;

  /// @brief True once the game is won.
  bool won_ = false;
  /// @brief Animation clock time of the win.
  float won_at_ = 0.0f;
  /// @brief True if the win set a new best time.
  bool new_best_ = false;
  /// @brief True once this game's result has been recorded.
  bool result_recorded_ = false;

  /// @brief The hint being shown, if any.
  std::optional<Move> hint_;
  /// @brief Animation clock time when the hint appeared.
  float hint_start_ = 0.0f;
  /// @brief The solver's answer, arriving from another thread.
  ///
  /// `std::future` is a handle to a value that another thread will produce
  /// later. `wait_for(0s)` asks "is it ready yet?" without blocking, and
  /// `get()` takes the value. Note: destroying a future made by
  /// `std::async` waits for its thread to finish, so the solver is always
  /// cancelled first.
  std::future<SolveResult> solve_future_;
  /// @brief Stop flag shared with the solver thread.
  ///
  /// `std::shared_ptr` keeps the flag alive for as long as *either* side
  /// holds it, so the thread can never read a flag that has been freed,
  /// even if the Game moves on to a new hint. `std::atomic<bool>` makes
  /// reads and writes from two threads at once safe.
  std::shared_ptr<std::atomic<bool>> solve_cancel_;
  /// @brief `table_version_` when the running solve started.
  int solve_version_ = -1;

  /// @brief The run being dragged, if any.
  std::optional<Drag> drag_;
  /// @brief Where the dragged run would land if dropped now.
  std::optional<Move> drop_;

  /// @brief Message shown at the bottom (empty = none).
  std::string message_;
  /// @brief Animation clock time when the message disappears.
  float message_until_ = 0.0f;

  /// @brief True while the "choose a deal" box is open.
  bool choosing_deal_ = false;
  /// @brief Digits typed into the box.
  std::string deal_input_;
  /// @brief True once Esc was pressed outside the box.
  bool quit_requested_ = false;
};

}  // namespace freecell

#endif  // FREECELL_GAME_H_
