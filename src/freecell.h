#ifndef FREECELL_FREECELL_H_
#define FREECELL_FREECELL_H_

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "card.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Number of free cells.
inline constexpr int kCellCount = 4;
/// @brief Number of foundation piles (one per suit, in Suit order).
inline constexpr int kFoundationCount = 4;
/// @brief Number of tableau columns.
inline constexpr int kColumnCount = 8;
/// @brief Lowest deal number.
inline constexpr int kMinDeal = 1;
/// @brief Highest deal number the game offers. The classic numbered deals
/// run from 1 to 32000; the shuffle works for any number up to 2^31 - 1.
inline constexpr int kMaxDeal = 1000000;

/// @brief The three kinds of pile on the table.
enum class PileKind : std::uint8_t {
  /// @brief A spare spot that holds one card.
  Cell,
  /// @brief A pile built up from Ace to King. Foundation `i` takes the suit
  /// `static_cast<Suit>(i)`.
  Foundation,
  /// @brief One of the eight columns, built down in alternating colours.
  Column,
};

/// @brief Names one pile on the table.
struct PileId {
  /// @brief Kind of pile.
  PileKind kind = PileKind::Column;
  /// @brief Which cell (0..3), foundation (0..3), or column (0..7).
  int index = 0;

  /// @brief Defaulted equality compares `kind` and `index`.
  bool operator==(const PileId&) const = default;
};

/// @brief One move of one card, or of a run of cards, from pile to pile.
struct Move {
  /// @brief Pile the cards leave.
  PileId from;
  /// @brief Position in `from` of the first (lowest) card that moves. Every
  /// card above it moves with it.
  int index = 0;
  /// @brief Pile the cards land on.
  PileId to;

  /// @brief Defaulted equality compares every member.
  bool operator==(const Move&) const = default;
};

/// @brief Every card on the table. In each pile the *last* element of the
/// vector is the top card. A cell is a vector too (holding 0 or 1 cards),
/// so every pile can be read the same way.
struct Position {
  /// @brief The four free cells.
  std::array<std::vector<Card>, kCellCount> cells;
  /// @brief The four foundations, indexed by suit.
  std::array<std::vector<Card>, kFoundationCount> foundations;
  /// @brief The eight columns.
  std::array<std::vector<Card>, kColumnCount> columns;

  /// @brief Defaulted equality compares every pile, card by card.
  bool operator==(const Position&) const = default;
};

/// @brief Returns true if `card` may go on top of `foundation`: an Ace on an
/// empty pile, or the next rank up of the same suit.
/// @param foundation Foundation pile.
/// @param card Card to place.
/// @return True if legal.
bool FitsFoundation(const std::vector<Card>& foundation, const Card& card);

/// @brief Returns true if `card` may go on top of `column`: anything on an
/// empty column, or one rank lower in the other colour.
/// @param column Column.
/// @param card Lowest card of the run to place.
/// @return True if legal.
bool FitsColumn(const std::vector<Card>& column, const Card& card);

/// @brief Returns true if `card` would go to its foundation automatically:
/// its foundation is ready for it, and no card that might still need it as
/// a place to build on is left (both foundations of the other colour already
/// hold the rank below it). Aces and twos are always safe.
/// @param foundations Every foundation.
/// @param card Card to check.
/// @return True if moving it up can never hurt.
bool IsSafeToFound(
    const std::array<std::vector<Card>, kFoundationCount>& foundations,
    const Card& card);

/// @brief Returns the most cards that can move as one run.
///
/// The rules only ever move one card at a time, but a run can be moved by
/// parking cards in free cells and empty columns along the way. With `f`
/// empty cells and `e` empty columns, that allows `(f + 1) * 2^e` cards.
/// @param free_cells Empty free cells.
/// @param empty_columns Empty columns, not counting the destination.
/// @return The longest run that can move.
int MaxRunLength(int free_cells, int empty_columns);

/// @brief The rules of FreeCell: dealing, moving, run moves, automatic
/// foundation moves, undo, and the win.
///
/// All 52 cards are dealt face up into eight columns. Four free cells each
/// hold one card. Foundations build up by suit from Ace to King; columns
/// build down in alternating colours, and any card can start an empty
/// column. Cards never come back off a foundation.
class FreeCell {
 public:
  /// @brief Creates an empty table. Call `Deal()` to start a game.
  FreeCell() = default;

  /// @brief Deals a numbered game.
  ///
  /// The shuffle is the classic numbered-deal shuffle, a simple linear
  /// congruential generator, so deal numbers 1 to 32000 give the same games
  /// as the well-known lists of numbered deals.
  /// @param number Deal number (clamped to `kMinDeal..kMaxDeal`).
  void Deal(int number);

  /// @brief Replaces the table with an exact position, for tests. Resets the
  /// move count and the undo history.
  /// @param position Cards to place.
  void LoadPosition(const Position& position);

  /// @brief Checks whether a move is legal.
  /// @param move Move to check.
  /// @return True if `ApplyMove(move)` would succeed.
  bool CanMove(const Move& move) const;

  /// @brief Makes a move if it is legal.
  /// @param move Move to make.
  /// @param automatic True for an automatic foundation move that follows a
  ///        player's move. Undo takes it back together with that move, and
  ///        it does not add to the move count.
  /// @return True if the move was made.
  bool ApplyMove(const Move& move, bool automatic = false);

  /// @brief Returns the next card that should go up to a foundation by
  /// itself (see `IsSafeToFound()`).
  /// @return The move, or `std::nullopt` if no card is safe to move up.
  std::optional<Move> NextSafeAutoMove() const;

  /// @brief Finds where a card should go when the player just clicks it:
  /// its foundation, else a column with cards, else an empty column, else a
  /// free cell.
  /// @param from Pile the card is in.
  /// @param index Position of the card in that pile.
  /// @return A legal move, or `std::nullopt` if the card cannot move.
  std::optional<Move> FindAutoMove(PileId from, int index) const;

  /// @brief Returns every legal single-card move, plus every legal run move
  /// between columns.
  /// @return The moves (empty when the player is stuck).
  std::vector<Move> LegalMoves() const;

  /// @brief Returns the longest run that could move onto `to` right now.
  /// @param to Destination pile.
  /// @return Number of cards.
  int RunCapacity(PileId to) const;

  /// @brief Takes back the last player move and the automatic moves that
  /// followed it.
  /// @return True if there was something to take back.
  bool Undo();

  /// @brief Returns true if `Undo()` would do something.
  /// @return True when the history is not empty.
  bool CanUndo() const { return !history_.empty(); }

  /// @brief Returns true when all 52 cards are on the foundations.
  /// @return True for a won game.
  bool IsWon() const;

  /// @brief Returns the cards in one pile.
  /// @param pile Pile to read.
  /// @return The pile, bottom card first. An invalid pile gives an empty
  ///         vector. The reference stays valid until the next change.
  const std::vector<Card>& Pile(PileId pile) const;

  /// @brief The whole table.
  const Position& position() const { return position_; }
  /// @brief The deal number, or `0` for a position loaded by hand.
  int deal_number() const { return deal_number_; }
  /// @brief Player moves made (undo takes them back).
  int moves() const { return moves_; }

 private:
  /// @brief Everything `Undo()` needs to restore one earlier state.
  struct Snapshot {
    /// @brief The table.
    Position position;
    /// @brief The move count.
    int moves = 0;
  };

  /// @brief Returns a writable reference to a pile.
  /// @param pile Pile to get (must be valid).
  /// @return Reference into `position_`.
  std::vector<Card>& MutablePile(PileId pile);

  /// @brief Every card on the table.
  Position position_;
  /// @brief Deal number, or 0.
  int deal_number_ = 0;
  /// @brief Player moves made.
  int moves_ = 0;
  /// @brief Earlier states, oldest first, for `Undo()`. Only player moves
  /// save a snapshot, so automatic moves are undone with them.
  std::vector<Snapshot> history_;
};

}  // namespace freecell

#endif  // FREECELL_FREECELL_H_
