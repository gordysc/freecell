#include "game.h"

#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "screen.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Timing constants and helpers internal to `game.cpp`.
namespace {

/// @brief Mouse travel in pixels before a press becomes a drag.
constexpr float kDragThreshold = 5.0f;
/// @brief Seconds between two cards being dealt.
constexpr float kDealInterval = 0.022f;
/// @brief Seconds before the first card is dealt.
constexpr float kDealDelay = 0.15f;
/// @brief Seconds between two automatic foundation moves.
constexpr float kAutoMoveSeconds = 0.09f;
/// @brief Seconds a hint stays on screen.
constexpr float kHintSeconds = 3.5f;
/// @brief Seconds a message stays on screen.
constexpr float kMessageSeconds = 3.5f;
/// @brief Seconds after the win before the cards start to fly.
constexpr float kCelebrationDelay = 0.6f;
/// @brief Most positions the hint solver may look at. Release builds get
/// through this in well under a second.
constexpr int kHintSearchLimit = 300000;
/// @brief Highest deal number picked for a random new game: the classic
/// numbered deals.
constexpr int kRandomDealMax = 32000;
/// @brief Most digits the "choose a deal" box accepts.
constexpr std::size_t kMaxDealDigits = 7;

/// @brief Returns the top-left corner of a rectangle.
/// @param rect Any rectangle.
/// @return Its top-left corner.
Vector2 Corner(Rectangle rect) { return {rect.x, rect.y}; }

/// @brief Returns true on the first frame `key` is pressed and on the
/// operating system's key-repeat frames while it is held.
/// @param key raylib key code.
/// @return True if the key should act this frame.
bool KeyPressedOrRepeated(int key) {
  return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

/// @brief Returns true if the cards from `index` up form a run (one rank
/// lower each time, in alternating colours), so they can be picked up.
/// @param pile Cards in the pile.
/// @param index First card.
/// @return True for a run.
bool IsRunFrom(const std::vector<Card>& pile, int index) {
  for (std::size_t i = static_cast<std::size_t>(index) + 1; i < pile.size();
       ++i) {
    if (pile[i].rank != pile[i - 1].rank - 1 ||
        IsRed(pile[i].suit) == IsRed(pile[i - 1].suit)) {
      return false;
    }
  }
  return true;
}

}  // namespace

Game::Game() : save_path_(DefaultSavePath()), save_(LoadSaveData(save_path_)) {
  audio_.set_muted(save_.muted);
  // `std::random_device{}()` reads one random number from the operating
  // system, so each launch starts on a different deal.
  std::mt19937 rng(std::random_device{}());
  StartDeal(std::uniform_int_distribution<int>(1, kRandomDealMax)(rng));
}

Game::~Game() {
  // Stop the solver thread and wait for it before anything it might touch
  // is destroyed. (It only touches its own copies, but waiting here also
  // keeps the future's destructor from blocking at an unexpected moment.)
  CancelHint();
  if (solve_future_.valid()) solve_future_.wait();
  RecordAbandonedGame();
  PersistSaveData();
}

void Game::Update(float dt) {
  // Clamp so a long stall (window drag, breakpoint) can't add a huge jump.
  dt = std::clamp(dt, 0.0f, 0.1f);
  now_ += dt;
  if (clock_running_) clock_ += dt;

  if (choosing_deal_) {
    HandleDealInput();
  } else {
    HandleKeyboard();
    HandleMouse();
  }
  PollHint();

  if (!won_ && !drag_ && now_ >= next_auto_move_) {
    if (const auto move = game_.NextSafeAutoMove()) {
      const int rank = game_.Pile(move->from).back().rank;
      game_.ApplyMove(*move, true);
      audio_.PlayFoundation(rank);
      next_auto_move_ = now_ + kAutoMoveSeconds;
      AfterChange();
    }
  }
  if (hint_ && now_ - hint_start_ > kHintSeconds) hint_.reset();

  SyncAnimator(dt);
  celebration_.Update(dt, now_);
}

void Game::Draw() const {
  FrameView view;
  view.now = now_;
  view.mouse = GetMousePosition();
  view.card_back = save_.card_back;
  view.muted = audio_.muted();
  view.seconds = clock_;
  view.record = save_;
  view.hint = hint_;
  view.hint_start = hint_start_;
  view.thinking = solve_future_.valid();
  if (drag_ && drag_->moved) view.dragging = PileHit{drag_->pile, drag_->index};
  if (drop_) view.drop_target = drop_->to;
  view.won = won_;
  view.won_at = won_at_;
  view.new_best = new_best_;
  view.message = message_;
  view.message_alpha = std::clamp((message_until_ - now_) / 0.4f, 0.0f, 1.0f);
  view.choosing_deal = choosing_deal_;
  view.deal_input = deal_input_;
  renderer_.Draw(game_, animator_, celebration_, textures_, view);
}

void Game::StartDeal(int number) {
  CancelHint();
  RecordAbandonedGame();
  game_.Deal(number);
  ++table_version_;
  clock_ = 0.0f;
  clock_running_ = false;
  won_ = false;
  new_best_ = false;
  result_recorded_ = false;
  hint_.reset();
  drag_.reset();
  drop_.reset();
  message_.clear();
  message_until_ = 0.0f;
  celebration_.Clear();

  // Dealing animation: every card waits face down below the window, then
  // flies to its column and turns over, in the order the cards are dealt.
  animator_.Reset();
  const Vector2 deck{(static_cast<float>(kScreenWidth) - kCardWidth) / 2.0f,
                     static_cast<float>(kScreenHeight) + 20.0f};
  for (int i = 0; i < kDeckSize; ++i) {
    const Card& card =
        game_.position().columns[static_cast<std::size_t>(i % kColumnCount)]
                                [static_cast<std::size_t>(i / kColumnCount)];
    animator_.Hold(card, deck, false,
                   now_ + kDealDelay + kDealInterval * static_cast<float>(i));
  }
  // Let the deal finish before any card goes up by itself.
  next_auto_move_ = now_ + kDealDelay + kDealInterval * kDeckSize + 0.4f;
  audio_.Play(SoundEffect::Deal);
}

void Game::HandleMouse() {
  const Vector2 mouse = GetMousePosition();

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    for (int i = 0; i < kButtonCount; ++i) {
      const auto button = static_cast<Button>(i);
      if (CheckCollisionPointRec(mouse, ButtonRect(button))) {
        PressButton(button);
        return;
      }
    }
    if (won_) {
      // Give the celebration a moment so a stray click can't skip it.
      if (now_ - won_at_ > 1.0f) PressButton(Button::NewGame);
      return;
    }
    const auto hit = HitTest(game_, mouse);
    if (hit && hit->index >= 0 && hit->pile.kind != PileKind::Foundation) {
      const std::vector<Card>& cards = game_.Pile(hit->pile);
      if (IsRunFrom(cards, hit->index)) {
        const Vector2 corner = Corner(CardRect(game_, hit->pile, hit->index));
        drag_ = Drag{hit->pile,
                     hit->index,
                     {mouse.x - corner.x, mouse.y - corner.y},
                     mouse,
                     false};
      } else {
        audio_.Play(SoundEffect::Invalid);
        ShowMessage("Only a run of alternating colours can move together.");
      }
    }
  }

  if (drag_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
    if (!drag_->moved &&
        std::hypot(mouse.x - drag_->press.x, mouse.y - drag_->press.y) >
            kDragThreshold) {
      drag_->moved = true;
      audio_.Play(SoundEffect::Pick);
    }
    if (drag_->moved) {
      const Rectangle held{mouse.x - drag_->grab.x, mouse.y - drag_->grab.y,
                           kCardWidth, kCardHeight};
      drop_ = FindDrop(game_, drag_->pile, drag_->index, held);
    }
  }

  if (drag_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    // Copy the drag out and clear it first, so the cards are no longer
    // "held" when the move below sends them flying to their new pile.
    const Drag drag = *drag_;
    const std::optional<Move> drop = drop_;
    drag_.reset();
    drop_.reset();
    if (drag.moved) {
      if (drop) {
        MakeMove(*drop);
      } else {
        // Not a legal spot: the animator slides the cards back home.
        audio_.Play(SoundEffect::Invalid);
        ExplainFailedDrop(drag.pile, drag.index);
      }
    } else if (const auto move = game_.FindAutoMove(drag.pile, drag.index)) {
      // A plain click sends the card wherever it fits best.
      MakeMove(*move);
    } else {
      audio_.Play(SoundEffect::Invalid);
      ExplainFailedDrop(drag.pile, drag.index);
    }
  }
}

void Game::HandleKeyboard() {
  if (IsKeyPressed(KEY_ESCAPE)) quit_requested_ = true;
  if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_F2)) PressButton(Button::NewGame);
  if (IsKeyPressed(KEY_R)) PressButton(Button::Restart);
  if (IsKeyPressed(KEY_U) || IsKeyPressed(KEY_Z) ||
      IsKeyPressed(KEY_BACKSPACE)) {
    UndoMove();
  }
  if (IsKeyPressed(KEY_H)) RequestHint();
  if (IsKeyPressed(KEY_G)) PressButton(Button::ChooseDeal);
  if (IsKeyPressed(KEY_B)) PressButton(Button::CardBack);
  if (IsKeyPressed(KEY_M)) PressButton(Button::Sound);
}

void Game::HandleDealInput() {
  // `GetCharPressed()` returns each typed character once (as a Unicode
  // code point), in order, then 0 when the queue is empty.
  for (int ch = GetCharPressed(); ch != 0; ch = GetCharPressed()) {
    if (ch >= '0' && ch <= '9' && deal_input_.size() < kMaxDealDigits) {
      deal_input_.push_back(static_cast<char>(ch));
    }
  }
  if (KeyPressedOrRepeated(KEY_BACKSPACE) && !deal_input_.empty()) {
    deal_input_.pop_back();
  }
  if (IsKeyPressed(KEY_ESCAPE)) choosing_deal_ = false;
  if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) &&
      !deal_input_.empty()) {
    choosing_deal_ = false;
    // At most 7 digits, so `std::stoi` cannot overflow and never throws
    // here; StartDeal clamps the number into range.
    StartDeal(std::stoi(deal_input_));
  }
}

void Game::PressButton(Button button) {
  switch (button) {
    case Button::NewGame: {
      std::mt19937 rng(std::random_device{}());
      StartDeal(std::uniform_int_distribution<int>(1, kRandomDealMax)(rng));
      break;
    }
    case Button::Restart:
      if (game_.CanUndo() && !won_) StartDeal(game_.deal_number());
      break;
    case Button::Undo:
      UndoMove();
      break;
    case Button::Hint:
      RequestHint();
      break;
    case Button::ChooseDeal:
      choosing_deal_ = true;
      deal_input_.clear();
      // Throw away characters typed before the box opened (like the "g"
      // that opened it).
      while (GetCharPressed() != 0) {
      }
      break;
    case Button::CardBack:
      save_.card_back = (save_.card_back + 1) % kCardBackCount;
      PersistSaveData();
      break;
    case Button::Sound:
      audio_.set_muted(!audio_.muted());
      save_.muted = audio_.muted();
      PersistSaveData();
      break;
  }
}

void Game::MakeMove(const Move& move) {
  const std::vector<Card>& source = game_.Pile(move.from);
  if (move.index < 0 || move.index >= static_cast<int>(source.size())) return;
  // Read the rank *before* the move: afterwards the card has moved.
  const int rank = source[static_cast<std::size_t>(move.index)].rank;
  if (!game_.ApplyMove(move)) {
    audio_.Play(SoundEffect::Invalid);
    return;
  }
  if (!won_) clock_running_ = true;
  if (move.to.kind == PileKind::Foundation) {
    audio_.PlayFoundation(rank);
  } else {
    audio_.Play(SoundEffect::Place);
  }
  // A short pause before automatic moves, so they read as a reaction.
  next_auto_move_ = now_ + kAutoMoveSeconds * 2.0f;
  AfterChange();
}

void Game::ExplainFailedDrop(PileId from, int index) {
  const std::vector<Card>& cards = game_.Pile(from);
  const int count = static_cast<int>(cards.size()) - index;
  if (count <= 1) return;
  const Card& first = cards[static_cast<std::size_t>(index)];
  // Is there a column the run would fit on, if only there were more room?
  for (int c = 0; c < kColumnCount; ++c) {
    const PileId column{PileKind::Column, c};
    if (column == from || !FitsColumn(game_.Pile(column), first)) continue;
    const int capacity = game_.RunCapacity(column);
    if (count > capacity) {
      // `std::to_string` turns a number into a `std::string`, and `+`
      // joins strings (it makes a new string each time).
      ShowMessage("Not enough free space to move " + std::to_string(count) +
                  " cards. You can move " + std::to_string(capacity) + ".");
      return;
    }
  }
}

void Game::UndoMove() {
  if (won_ || drag_) return;
  if (game_.Undo()) {
    audio_.Play(SoundEffect::Undo);
    // Give the player a moment before cards go up by themselves again.
    next_auto_move_ = now_ + 0.5f;
    AfterChange();
  }
}

void Game::RequestHint() {
  if (won_ || solve_future_.valid()) return;
  FlushAutoMoves();
  // A fresh flag for this solve. The lambda below captures its own copy of
  // the shared_ptr *by value*, so the flag lives as long as the thread
  // needs it. It also captures a copy of the position by value: the thread
  // never looks at `game_`, which keeps changing on this thread.
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  solve_cancel_ = cancel;
  solve_version_ = table_version_;
  const Position position = game_.position();
  // `std::async` with `std::launch::async` runs the lambda on a new thread
  // and returns a future for its result.
  solve_future_ = std::async(std::launch::async, [position, cancel]() {
    return Solve(position, kHintSearchLimit, cancel.get());
  });
}

void Game::PollHint() {
  if (!solve_future_.valid()) return;
  using namespace std::chrono_literals;  // Lets us write `0s` for 0 seconds.
  if (solve_future_.wait_for(0s) != std::future_status::ready) return;
  // `get()` moves the result out; afterwards the future is no longer valid.
  const SolveResult result = solve_future_.get();
  if (solve_version_ != table_version_ ||
      result.status == SolveStatus::Cancelled) {
    return;  // The table changed while the solver worked: answer is stale.
  }
  switch (result.status) {
    case SolveStatus::Solved:
      if (!result.moves.empty()) {
        hint_ = result.moves.front();
        hint_start_ = now_;
        audio_.Play(SoundEffect::Hint);
        ShowMessage("You can still win from here.");
      }
      break;
    case SolveStatus::Unsolvable:
      audio_.Play(SoundEffect::Invalid);
      ShowMessage("There is no way to win from here. Undo some moves.");
      break;
    case SolveStatus::GaveUp:
    case SolveStatus::Cancelled: {
      // No sure win found in time: suggest a reasonable legal move instead,
      // preferring the foundations, then moves between columns.
      const std::vector<Move> moves = game_.LegalMoves();
      auto pick = [&moves](PileKind to) -> std::optional<Move> {
        for (const Move& move : moves) {
          if (move.to.kind == to) return move;
        }
        return std::nullopt;
      };
      hint_ = pick(PileKind::Foundation);
      if (!hint_) hint_ = pick(PileKind::Column);
      if (!hint_) hint_ = pick(PileKind::Cell);
      hint_start_ = now_;
      if (hint_) {
        audio_.Play(SoundEffect::Hint);
        ShowMessage("No sure win found quickly. This move is possible.");
      } else {
        ShowMessage("No moves left. Undo or start a new game.");
      }
      break;
    }
  }
}

void Game::CancelHint() {
  // `store(true)` writes the flag atomically; the solver thread sees it at
  // its next check and returns early.
  if (solve_cancel_) solve_cancel_->store(true);
}

void Game::FlushAutoMoves() {
  bool moved = false;
  while (const auto move = game_.NextSafeAutoMove()) {
    game_.ApplyMove(*move, true);
    moved = true;
  }
  if (moved) AfterChange();
}

void Game::AfterChange() {
  ++table_version_;
  hint_.reset();
  CancelHint();
  if (game_.IsWon()) {
    Win();
    return;
  }
  if (game_.LegalMoves().empty() && !game_.NextSafeAutoMove()) {
    ShowMessage("No moves left. Undo or start a new game.");
  }
}

void Game::Win() {
  if (won_) return;
  won_ = true;
  won_at_ = now_;
  clock_running_ = false;
  message_.clear();
  if (!result_recorded_) {
    result_recorded_ = true;
    new_best_ = RecordResult(save_, {true, clock_, game_.moves()});
    PersistSaveData();
  }
  audio_.Play(SoundEffect::Win);

  // Cards leave the foundations from the top: every King, then every Queen,
  // and so on, taking turns between the four piles.
  std::vector<FlyingCard> flying;
  for (int rank = kKing; rank >= kAce; --rank) {
    for (int f = 0; f < kFoundationCount; ++f) {
      const PileId pile{PileKind::Foundation, f};
      const std::vector<Card>& cards = game_.Pile(pile);
      if (static_cast<int>(cards.size()) < rank) continue;
      const Rectangle rect = CardRect(game_, pile, rank - 1);
      FlyingCard card;
      card.card = cards[static_cast<std::size_t>(rank - 1)];
      card.position = {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
      flying.push_back(card);
    }
  }
  celebration_.Start(flying, now_ + kCelebrationDelay);
}

void Game::SyncAnimator(float dt) {
  const Vector2 mouse = GetMousePosition();
  // Tells the animator where every card of one pile belongs. Held cards
  // follow the mouse exactly, keeping their spacing in the column.
  auto sync_pile = [&](PileId pile) {
    const std::vector<Card>& cards = game_.Pile(pile);
    for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
      Vector2 target = Corner(CardRect(game_, pile, i));
      const bool held =
          drag_ && drag_->moved && drag_->pile == pile && i >= drag_->index;
      if (held) {
        const Vector2 first = Corner(CardRect(game_, pile, drag_->index));
        target = {mouse.x - drag_->grab.x + (target.x - first.x),
                  mouse.y - drag_->grab.y + (target.y - first.y)};
      }
      animator_.Follow(cards[static_cast<std::size_t>(i)], target, held, now_,
                       dt);
    }
  };
  for (int c = 0; c < kCellCount; ++c) sync_pile({PileKind::Cell, c});
  for (int f = 0; f < kFoundationCount; ++f) {
    sync_pile({PileKind::Foundation, f});
  }
  for (int c = 0; c < kColumnCount; ++c) sync_pile({PileKind::Column, c});
}

void Game::ShowMessage(std::string text) {
  // `text` arrives by value; `std::move` hands its buffer to `message_`
  // instead of copying it. `text` is left empty, and is not used again.
  message_ = std::move(text);
  message_until_ = now_ + kMessageSeconds;
}

void Game::RecordAbandonedGame() {
  if (result_recorded_ || won_ || game_.moves() == 0) return;
  result_recorded_ = true;
  RecordResult(save_, {false, clock_, game_.moves()});
  PersistSaveData();
}

void Game::PersistSaveData() { WriteSaveData(save_path_, save_); }

}  // namespace freecell
