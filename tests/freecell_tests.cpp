#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <vector>

#include "card.h"
#include "freecell.h"
#include "solver.h"
#include "storage.h"

/// @brief Minimal self-contained unit test suite for `freecell_core`.
namespace {

// `using` declarations bring a few names into this namespace so the tests
// can write `FreeCell` instead of `freecell::FreeCell`.
using freecell::Card;
using freecell::FreeCell;
using freecell::Move;
using freecell::PileId;
using freecell::PileKind;
using freecell::Position;
using freecell::Suit;

/// @brief Total number of assertions that failed during this test run.
int g_failures = 0;

/// @brief Checks a boolean condition and prints a diagnostic message on
/// failure.
/// @param condition Expression expected to be true.
/// @param message Human-readable failure description.
void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
  }
}

/// @brief Makes a face-up card.
/// @param rank Rank from 1 (Ace) to 13 (King).
/// @param suit Suit.
/// @return The card.
Card C(int rank, Suit suit) { return {suit, rank, true}; }

/// @brief Shorthand for free cell `i`.
/// @param i Cell number.
/// @return The PileId.
constexpr PileId Cell(int i) { return {PileKind::Cell, i}; }
/// @brief Shorthand for the foundation of `suit`.
/// @param suit Suit.
/// @return The PileId.
constexpr PileId F(Suit suit) {
  return {PileKind::Foundation, static_cast<int>(suit)};
}
/// @brief Shorthand for column `i`.
/// @param i Column number.
/// @return The PileId.
constexpr PileId Col(int i) { return {PileKind::Column, i}; }

/// @brief Returns true if two cards have the same rank and suit.
/// @param a First card.
/// @param b Second card.
/// @return True for the same card.
bool Same(const Card& a, const Card& b) {
  return a.rank == b.rank && a.suit == b.suit;
}

/// @brief Checks that the table holds each of the 52 cards exactly once.
/// @param game Game to check.
/// @return True if every card is there once.
bool HasFullDeck(const FreeCell& game) {
  std::array<int, freecell::kDeckSize> seen{};
  int total = 0;
  const Position& p = game.position();
  // Counts each card it is given. Captures the locals by reference.
  auto count = [&](const std::vector<Card>& pile) {
    for (const Card& card : pile) {
      ++seen[static_cast<std::size_t>(freecell::CardId(card))];
      ++total;
    }
  };
  for (const auto& pile : p.cells) count(pile);
  for (const auto& pile : p.foundations) count(pile);
  for (const auto& pile : p.columns) count(pile);
  if (total != freecell::kDeckSize) return false;
  for (const int n : seen) {
    if (n != 1) return false;
  }
  return true;
}

/// @brief Plays every safe foundation move, as the game does after each
/// player move.
/// @param game Game to update.
void PlayAutoMoves(FreeCell& game) {
  while (const auto move = game.NextSafeAutoMove()) {
    game.ApplyMove(*move, true);
  }
}

/// @brief Verifies deal 1 matches the classic numbered deal.
void TestClassicDealOne() {
  FreeCell game;
  game.Deal(1);
  const Position& p = game.position();
  // Deal 1, first row: JD 2D 9H JC 5D 7H 7C 5H.
  const std::array<Card, 8> first_row{
      C(11, Suit::Diamonds), C(2, Suit::Diamonds), C(9, Suit::Hearts),
      C(11, Suit::Clubs),    C(5, Suit::Diamonds), C(7, Suit::Hearts),
      C(7, Suit::Clubs),     C(5, Suit::Hearts)};
  bool row_ok = true;
  for (std::size_t c = 0; c < first_row.size(); ++c) {
    row_ok = row_ok && Same(p.columns[c][0], first_row[c]);
  }
  Expect(row_ok, "Deal 1 has the classic first row");
  // Deal 1, last row: 6S 9C 2H 6H.
  Expect(Same(p.columns[0][6], C(6, Suit::Spades)) &&
             Same(p.columns[1][6], C(9, Suit::Clubs)) &&
             Same(p.columns[2][6], C(2, Suit::Hearts)) &&
             Same(p.columns[3][6], C(6, Suit::Hearts)),
         "Deal 1 has the classic last row");
  bool sizes_ok = true;
  for (int c = 0; c < freecell::kColumnCount; ++c) {
    sizes_ok = sizes_ok && p.columns[static_cast<std::size_t>(c)].size() ==
                               (c < 4 ? 7U : 6U);
  }
  Expect(sizes_ok, "Columns 1-4 get 7 cards and columns 5-8 get 6");
  Expect(HasFullDeck(game), "The deal uses all 52 cards once");
  Expect(game.deal_number() == 1 && game.moves() == 0 && !game.CanUndo(),
         "A deal starts at move 0 with no history");

  FreeCell again;
  again.Deal(1);
  FreeCell other;
  other.Deal(2);
  Expect(game.position() == again.position(), "A deal number is repeatable");
  Expect(!(game.position() == other.position()), "Other numbers differ");
  other.Deal(0);
  Expect(other.deal_number() == freecell::kMinDeal, "Deal numbers clamp");
}

/// @brief Verifies where single cards may go.
void TestBasicRules() {
  Position p;
  p.columns[0] = {C(9, Suit::Spades)};
  p.columns[1] = {C(8, Suit::Hearts)};
  p.columns[2] = {C(8, Suit::Clubs)};
  p.columns[3] = {C(1, Suit::Hearts)};
  p.cells[0] = {C(13, Suit::Diamonds)};
  p.foundations[static_cast<std::size_t>(Suit::Spades)] = {C(1, Suit::Spades)};
  FreeCell game;
  game.LoadPosition(p);
  Expect(game.CanMove({Col(1), 0, Col(0)}), "A red 8 goes on a black 9");
  Expect(!game.CanMove({Col(2), 0, Col(0)}), "Same colours do not stack");
  Expect(game.CanMove({Col(2), 0, Col(5)}), "Any card goes on an empty column");
  Expect(game.CanMove({Col(2), 0, Cell(1)}), "A card goes in an empty cell");
  Expect(!game.CanMove({Col(2), 0, Cell(0)}), "A cell holds one card");
  Expect(game.CanMove({Col(3), 0, F(Suit::Hearts)}),
         "An Ace goes to its own foundation");
  Expect(!game.CanMove({Col(3), 0, F(Suit::Clubs)}),
         "An Ace never goes to another suit's foundation");
  Expect(!game.CanMove({F(Suit::Spades), 0, Col(1)}),
         "Cards never leave a foundation");
  Expect(game.CanMove({Cell(0), 0, Col(6)}),
         "A card in a cell can go to a column");
  Expect(!game.CanMove({Col(0), 0, Col(0)}), "A pile can't move onto itself");
  Expect(!game.CanMove({Col(0), 0, Col(12)}), "Columns past 7 do not exist");
}

/// @brief Verifies run moves and their size limit.
void TestRunMoves() {
  Expect(freecell::MaxRunLength(0, 0) == 1 &&
             freecell::MaxRunLength(4, 0) == 5 &&
             freecell::MaxRunLength(1, 1) == 4 &&
             freecell::MaxRunLength(4, 2) == 20,
         "Run capacity is (free cells + 1) x 2^(empty columns)");

  Position p;
  // A run of 4 (9 8 7 6, alternating) under a target 10.
  p.columns[0] = {C(13, Suit::Clubs), C(9, Suit::Spades), C(8, Suit::Hearts),
                  C(7, Suit::Clubs), C(6, Suit::Diamonds)};
  p.columns[1] = {C(10, Suit::Hearts)};
  for (int c = 2; c < freecell::kColumnCount; ++c) {
    p.columns[static_cast<std::size_t>(c)] = {C(c, Suit::Hearts)};
  }
  p.cells[0] = {C(12, Suit::Spades)};
  p.cells[1] = {C(12, Suit::Hearts)};
  FreeCell game;
  game.LoadPosition(p);
  // Two free cells and no empty column: 3 cards at a time.
  Expect(game.RunCapacity(Col(1)) == 3, "Two free cells allow 3 cards");
  Expect(!game.CanMove({Col(0), 1, Col(1)}), "A run of 4 is too long");
  Expect(game.CanMove({Col(0), 2, Col(1)}) == false,
         "An 8 does not go on a 10");
  Expect(!game.CanMove({Col(0), 0, Col(1)}), "A broken run can't move");

  p.cells[1].clear();
  game.LoadPosition(p);
  Expect(game.RunCapacity(Col(1)) == 4, "Three free cells allow 4 cards");
  Expect(game.ApplyMove({Col(0), 1, Col(1)}), "The run of 4 moves");
  Expect(game.position().columns[1].size() == 5 &&
             game.position().columns[0].size() == 1,
         "All four cards moved together");
  Expect(game.moves() == 1, "A run move counts as one move");

  // Into an empty column, that column can't also park cards.
  Position q;
  q.columns[0] = {C(9, Suit::Spades), C(8, Suit::Hearts), C(7, Suit::Clubs)};
  for (int c = 1; c < 6; ++c) {
    q.columns[static_cast<std::size_t>(c)] = {C(c, Suit::Hearts)};
  }
  q.cells = {{{C(13, Suit::Clubs)}, {C(13, Suit::Spades)}, {}, {}}};
  game.LoadPosition(q);
  Expect(game.RunCapacity(Col(6)) == 6,
         "Into an empty column, only the other empty column doubles");
  Expect(game.RunCapacity(Col(1)) == 12, "Elsewhere both empty columns count");
}

/// @brief Verifies the rule for automatic foundation moves.
void TestSafeAutoMoves() {
  std::array<std::vector<Card>, freecell::kFoundationCount> f;
  for (int rank = 1; rank <= 4; ++rank) {
    f[static_cast<std::size_t>(Suit::Clubs)].push_back(C(rank, Suit::Clubs));
    f[static_cast<std::size_t>(Suit::Spades)].push_back(C(rank, Suit::Spades));
  }
  for (int rank = 1; rank <= 3; ++rank) {
    f[static_cast<std::size_t>(Suit::Hearts)].push_back(C(rank, Suit::Hearts));
  }
  Expect(freecell::IsSafeToFound(f, C(4, Suit::Hearts)),
         "4 of hearts is safe once both black 3s are up");
  Expect(!freecell::IsSafeToFound(f, C(5, Suit::Clubs)),
         "5 of clubs waits for the red 4s");
  Expect(freecell::IsSafeToFound(f, C(1, Suit::Diamonds)), "Aces are safe");
  Expect(!freecell::IsSafeToFound(f, C(2, Suit::Diamonds)),
         "A 2 must wait for its Ace");
}

/// @brief Verifies that undo takes back a move with its automatic moves.
void TestUndoIncludesAutoMoves() {
  Position p;
  p.columns[0] = {C(1, Suit::Hearts), C(5, Suit::Clubs)};
  p.columns[1] = {C(6, Suit::Diamonds)};
  FreeCell game;
  game.LoadPosition(p);
  const Position start = game.position();
  Expect(game.ApplyMove({Col(0), 1, Col(1)}),
         "5 of clubs goes on 6 of diamonds");
  const auto automatic = game.NextSafeAutoMove();
  Expect(automatic && automatic->to == F(Suit::Hearts),
         "The uncovered Ace goes up by itself");
  PlayAutoMoves(game);
  Expect(game.position()
                 .foundations[static_cast<std::size_t>(Suit::Hearts)]
                 .size() == 1,
         "The Ace is on its foundation");
  Expect(game.moves() == 1, "Automatic moves do not count as moves");
  Expect(game.Undo() && game.position() == start,
         "One undo takes back the move and the automatic move");
}

/// @brief Verifies the one-click move choices.
void TestClickMoves() {
  Position p;
  p.columns[0] = {C(1, Suit::Spades)};
  p.columns[1] = {C(9, Suit::Clubs)};
  p.columns[2] = {C(8, Suit::Diamonds)};
  p.columns[3] = {C(4, Suit::Clubs)};
  for (int c = 4; c < freecell::kColumnCount; ++c) {
    p.columns[static_cast<std::size_t>(c)] = {C(13 - c, Suit::Hearts)};
  }
  FreeCell game;
  game.LoadPosition(p);
  const auto ace = game.FindAutoMove(Col(0), 0);
  Expect(ace && *ace == Move{Col(0), 0, F(Suit::Spades)}, "An Ace goes up");
  const auto eight = game.FindAutoMove(Col(2), 0);
  Expect(eight && *eight == Move{Col(2), 0, Col(1)}, "An 8 goes on a 9");
  const auto four = game.FindAutoMove(Col(3), 0);
  Expect(four && four->to.kind == PileKind::Cell,
         "With no column for it, a card goes to a free cell");
}

/// @brief Verifies a position with no legal move.
void TestStuck() {
  Position p;
  p.cells = {{{C(13, Suit::Spades)},
              {C(13, Suit::Hearts)},
              {C(13, Suit::Clubs)},
              {C(13, Suit::Diamonds)}}};
  const std::array<Card, 8> tops{C(5, Suit::Clubs),  C(5, Suit::Spades),
                                 C(5, Suit::Hearts), C(5, Suit::Diamonds),
                                 C(9, Suit::Clubs),  C(9, Suit::Spades),
                                 C(9, Suit::Hearts), C(9, Suit::Diamonds)};
  for (std::size_t c = 0; c < tops.size(); ++c) p.columns[c] = {tops[c]};
  FreeCell game;
  game.LoadPosition(p);
  Expect(game.LegalMoves().empty(), "No legal move exists");
  const auto result = freecell::Solve(p, 1000);
  Expect(result.status == freecell::SolveStatus::Unsolvable,
         "The solver proves a dead position can't be won");
}

/// @brief Solves several deals and replays each winning line through the
/// rules, which checks the solver and the rules against each other.
void TestSolverWinsDeals() {
  for (int deal = 1; deal <= 12; ++deal) {
    FreeCell game;
    game.Deal(deal);
    const auto result = freecell::Solve(game.position(), 200000);
    if (result.status != freecell::SolveStatus::Solved) {
      std::cerr << "  deal " << deal << " status "
                << static_cast<int>(result.status) << " after "
                << result.positions << " positions\n";
      Expect(false, "The solver solves deals 1 to 12");
      continue;
    }
    PlayAutoMoves(game);
    bool legal = true;
    for (const Move& move : result.moves) {
      legal = legal && game.ApplyMove(move);
      PlayAutoMoves(game);
    }
    Expect(legal, "Every move in the winning line is legal");
    Expect(game.IsWon(), "The winning line wins the game");
    Expect(HasFullDeck(game), "No card is lost along the way");
  }
}

/// @brief Verifies that the solver can be cancelled.
void TestSolverCancel() {
  FreeCell game;
  game.Deal(5);
  const std::atomic<bool> cancel{true};
  const auto result = freecell::Solve(game.position(), 200000, &cancel);
  Expect(result.status == freecell::SolveStatus::Cancelled ||
             result.status == freecell::SolveStatus::Solved,
         "A cancelled search stops early (or had already finished)");
}

/// @brief Verifies records round-trip through a save file on disk.
void TestStorageRoundTrip() {
  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() / "freecell_test.cfg";
  freecell::SaveData out;
  out.card_back = 3;
  out.muted = true;
  Expect(!freecell::RecordResult(out, {false, 30.0f, 12}),
         "A game given up sets no best");
  Expect(freecell::RecordResult(out, {true, 245.5f, 110}),
         "The first win sets a best time");
  Expect(!freecell::RecordResult(out, {true, 300.0f, 95}),
         "A slower win is not a best time");
  Expect(out.games == 3 && out.wins == 2 && out.streak == 2 &&
             out.best_streak == 2 && out.best_moves == 95,
         "Games, wins, streaks, and fewest moves are counted");
  Expect(freecell::WriteSaveData(temp_path, out), "WriteSaveData succeeds");
  const freecell::SaveData in = freecell::LoadSaveData(temp_path);
  Expect(in.card_back == 3 && in.muted && in.games == 3 && in.wins == 2 &&
             in.streak == 2 && in.best_streak == 2 && in.best_moves == 95 &&
             std::abs(in.best_time - 245.5f) < 0.01f,
         "Every field round-trips");
  // `std::error_code` overload: reports failure through `ec` instead of
  // throwing, so cleanup can never abort the test run.
  std::error_code ec;
  std::filesystem::remove(temp_path, ec);
}

}  // namespace

/// @brief Entry point for the CTest unit test binary.
/// @return 0 if all tests pass, non-zero otherwise.
int main() {
  TestClassicDealOne();
  TestBasicRules();
  TestRunMoves();
  TestSafeAutoMoves();
  TestUndoIncludesAutoMoves();
  TestClickMoves();
  TestStuck();
  TestSolverWinsDeals();
  TestSolverCancel();
  TestStorageRoundTrip();

  if (g_failures > 0) {
    std::cerr << g_failures << " test assertion(s) failed.\n";
    return 1;
  }
  std::cout << "All freecell_core tests passed!\n";
  return 0;
}
