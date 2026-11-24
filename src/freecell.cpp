#include "freecell.h"

#include <algorithm>
#include <utility>

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Rule helpers internal to `freecell.cpp`.
namespace {

/// @brief The empty pile that `FreeCell::Pile()` returns for a bad PileId.
///
/// `Pile()` returns a reference, so it needs an object that outlives every
/// call. A namespace-scope constant lives for the whole program, so a
/// reference to it can never dangle.
const std::vector<Card> kEmptyPile;

/// @brief Returns true if `pile` names a pile that exists.
/// @param pile Pile to check.
/// @return False for an out-of-range index.
bool IsValidPile(PileId pile) {
  switch (pile.kind) {
    case PileKind::Cell:
      return pile.index >= 0 && pile.index < kCellCount;
    case PileKind::Foundation:
      return pile.index >= 0 && pile.index < kFoundationCount;
    case PileKind::Column:
      return pile.index >= 0 && pile.index < kColumnCount;
  }
  return false;
}

/// @brief Returns true if the cards from `index` to the top of `pile` form
/// a run: each card one rank lower than the one under it, in the other
/// colour.
/// @param pile Pile to check.
/// @param index First card of the run.
/// @return True for a legal run (a single card always is one).
bool IsRun(const std::vector<Card>& pile, int index) {
  for (std::size_t i = static_cast<std::size_t>(index) + 1; i < pile.size();
       ++i) {
    const Card& below = pile[i - 1];
    const Card& above = pile[i];
    if (above.rank != below.rank - 1 ||
        IsRed(above.suit) == IsRed(below.suit)) {
      return false;
    }
  }
  return true;
}

/// @brief Returns a pile id.
/// @param kind Kind of pile.
/// @param index Pile number.
/// @return The PileId.
constexpr PileId Id(PileKind kind, int index) { return {kind, index}; }

}  // namespace

bool FitsFoundation(const std::vector<Card>& foundation, const Card& card) {
  if (foundation.empty()) return card.rank == kAce;
  const Card& top = foundation.back();
  return top.suit == card.suit && card.rank == top.rank + 1;
}

bool FitsColumn(const std::vector<Card>& column, const Card& card) {
  if (column.empty()) return true;
  const Card& top = column.back();
  return IsRed(top.suit) != IsRed(card.suit) && card.rank == top.rank - 1;
}

bool IsSafeToFound(
    const std::array<std::vector<Card>, kFoundationCount>& foundations,
    const Card& card) {
  if (!FitsFoundation(foundations[static_cast<std::size_t>(card.suit)], card)) {
    return false;
  }
  if (card.rank <= 2) return true;
  // A red 7 is only useful in a column as a home for a black 6. Once both
  // black 6s are on their foundations, nothing can ever want the red 7.
  for (int s = 0; s < kSuitCount; ++s) {
    if (IsRed(static_cast<Suit>(s)) == IsRed(card.suit)) continue;
    if (static_cast<int>(foundations[static_cast<std::size_t>(s)].size()) <
        card.rank - 1) {
      return false;
    }
  }
  return true;
}

int MaxRunLength(int free_cells, int empty_columns) {
  // `1 << n` is 2 to the power n: each empty column doubles the run, because
  // half the run can be parked there while the other half moves.
  return (free_cells + 1) * (1 << std::max(0, empty_columns));
}

void FreeCell::Deal(int number) {
  number = std::clamp(number, kMinDeal, kMaxDeal);
  // The classic numbered-deal shuffle. A linear congruential generator
  // (LCG) makes each new number from the last one with one multiply and one
  // add. Unsigned arithmetic wraps around at 2^32 by definition, and the
  // mask keeps the low 31 bits, so this is exact and well-defined.
  std::uint32_t seed = static_cast<std::uint32_t>(number);
  auto next_random = [&seed]() {
    seed = (seed * 214013U + 2531011U) & 0x7fffffffU;
    return seed >> 16U;
  };
  // Card numbers 0..51 go Ace of clubs, Ace of diamonds, Ace of hearts, Ace
  // of spades, 2 of clubs, ... The deck starts in reverse order.
  std::array<int, kDeckSize> deck{};
  for (int i = 0; i < kDeckSize; ++i) {
    deck[static_cast<std::size_t>(i)] = kDeckSize - 1 - i;
  }
  for (int i = 0; i < kDeckSize - 1; ++i) {
    const auto j = static_cast<std::size_t>(
        kDeckSize - 1 -
        static_cast<int>(next_random() %
                         static_cast<std::uint32_t>(kDeckSize - i)));
    std::swap(deck[static_cast<std::size_t>(i)], deck[j]);
  }

  Position position;
  // Deal left to right, one row at a time: the first four columns get 7
  // cards and the last four get 6.
  for (int i = 0; i < kDeckSize; ++i) {
    const int value = deck[static_cast<std::size_t>(i)];
    const Card card{static_cast<Suit>(value % 4), value / 4 + 1, true};
    position.columns[static_cast<std::size_t>(i % kColumnCount)].push_back(
        card);
  }
  LoadPosition(position);
  deal_number_ = number;
}

void FreeCell::LoadPosition(const Position& position) {
  position_ = position;
  deal_number_ = 0;
  moves_ = 0;
  history_.clear();
}

bool FreeCell::CanMove(const Move& move) const {
  if (!IsValidPile(move.from) || !IsValidPile(move.to)) return false;
  if (move.from == move.to) return false;
  // Cards never come back off a foundation in FreeCell.
  if (move.from.kind == PileKind::Foundation) return false;
  const std::vector<Card>& source = Pile(move.from);
  const int size = static_cast<int>(source.size());
  if (move.index < 0 || move.index >= size) return false;
  if (!IsRun(source, move.index)) return false;
  const int count = size - move.index;
  const Card& moving = source[static_cast<std::size_t>(move.index)];
  const std::vector<Card>& target = Pile(move.to);

  switch (move.to.kind) {
    case PileKind::Cell:
      return count == 1 && target.empty();
    case PileKind::Foundation:
      return count == 1 && move.to.index == static_cast<int>(moving.suit) &&
             FitsFoundation(target, moving);
    case PileKind::Column:
      return FitsColumn(target, moving) && count <= RunCapacity(move.to);
  }
  return false;
}

bool FreeCell::ApplyMove(const Move& move, bool automatic) {
  if (!CanMove(move)) return false;
  if (!automatic) {
    // A full copy of the table is only 52 small cards, so storing whole
    // snapshots is simpler and safer than recording how to reverse a move.
    history_.push_back({position_, moves_});
    ++moves_;
  }
  // Two references into two *different* vectors (CanMove rejected
  // `from == to`), so changing one never invalidates the other.
  std::vector<Card>& source = MutablePile(move.from);
  std::vector<Card>& target = MutablePile(move.to);
  const auto first = source.begin() + move.index;
  // Copy the run onto the target, then cut it off the source. Erasing
  // invalidates iterators at and after `first`, so `first` is not used
  // after the erase.
  target.insert(target.end(), first, source.end());
  source.erase(first, source.end());
  return true;
}

std::optional<Move> FreeCell::NextSafeAutoMove() const {
  // Tries the top card of one pile. Captures `this` (to read the table)
  // by reference through `[&]`; it is only used inside this function.
  auto try_pile = [&](PileId pile) -> std::optional<Move> {
    const std::vector<Card>& cards = Pile(pile);
    if (cards.empty()) return std::nullopt;
    const Card& top = cards.back();
    if (!IsSafeToFound(position_.foundations, top)) return std::nullopt;
    return Move{pile, static_cast<int>(cards.size()) - 1,
                Id(PileKind::Foundation, static_cast<int>(top.suit))};
  };
  for (int c = 0; c < kCellCount; ++c) {
    if (auto move = try_pile(Id(PileKind::Cell, c))) return move;
  }
  for (int c = 0; c < kColumnCount; ++c) {
    if (auto move = try_pile(Id(PileKind::Column, c))) return move;
  }
  return std::nullopt;
}

std::optional<Move> FreeCell::FindAutoMove(PileId from, int index) const {
  const std::vector<Card>& source = Pile(from);
  const int size = static_cast<int>(source.size());
  if (index < 0 || index >= size || from.kind == PileKind::Foundation) {
    return std::nullopt;
  }
  if (index == size - 1) {
    const Card& card = source.back();
    const Move up{from, index,
                  Id(PileKind::Foundation, static_cast<int>(card.suit))};
    if (CanMove(up)) return up;
  }
  std::optional<Move> empty_column;
  for (int c = 0; c < kColumnCount; ++c) {
    const Move move{from, index, Id(PileKind::Column, c)};
    if (!CanMove(move)) continue;
    if (!Pile(move.to).empty()) return move;
    // A run that already starts a column gains nothing from moving to
    // another empty one.
    const bool pointless = from.kind == PileKind::Column && index == 0;
    if (!pointless && !empty_column) empty_column = move;
  }
  if (empty_column) return empty_column;
  if (from.kind == PileKind::Column && index == size - 1) {
    for (int c = 0; c < kCellCount; ++c) {
      const Move move{from, index, Id(PileKind::Cell, c)};
      if (CanMove(move)) return move;
    }
  }
  return std::nullopt;
}

std::vector<Move> FreeCell::LegalMoves() const {
  std::vector<Move> moves;
  // Every pile a card can land on.
  std::vector<PileId> targets;
  for (int i = 0; i < kCellCount; ++i) targets.push_back(Id(PileKind::Cell, i));
  for (int i = 0; i < kFoundationCount; ++i) {
    targets.push_back(Id(PileKind::Foundation, i));
  }
  for (int i = 0; i < kColumnCount; ++i) {
    targets.push_back(Id(PileKind::Column, i));
  }
  auto add_from = [&](PileId from) {
    const int size = static_cast<int>(Pile(from).size());
    for (int index = 0; index < size; ++index) {
      for (const PileId& to : targets) {
        const Move move{from, index, to};
        if (CanMove(move)) moves.push_back(move);
      }
    }
  };
  for (int i = 0; i < kCellCount; ++i) add_from(Id(PileKind::Cell, i));
  for (int i = 0; i < kColumnCount; ++i) add_from(Id(PileKind::Column, i));
  return moves;
}

int FreeCell::RunCapacity(PileId to) const {
  // `std::ranges::count_if` counts the piles the lambda accepts.
  const auto is_empty = [](const std::vector<Card>& pile) {
    return pile.empty();
  };
  const auto free_cells =
      static_cast<int>(std::ranges::count_if(position_.cells, is_empty));
  auto empty_columns =
      static_cast<int>(std::ranges::count_if(position_.columns, is_empty));
  // The destination can't also be a parking spot for the same move.
  if (to.kind == PileKind::Column && Pile(to).empty()) --empty_columns;
  return MaxRunLength(free_cells, empty_columns);
}

bool FreeCell::Undo() {
  if (history_.empty()) return false;
  // `std::move` takes the snapshot's piles instead of copying them; the
  // moved-from snapshot is popped straight after, so nobody sees it.
  Snapshot& last = history_.back();
  position_ = std::move(last.position);
  moves_ = last.moves;
  history_.pop_back();
  return true;
}

bool FreeCell::IsWon() const {
  return std::ranges::all_of(
      position_.foundations,
      [](const std::vector<Card>& f) { return f.size() == kRankCount; });
}

const std::vector<Card>& FreeCell::Pile(PileId pile) const {
  if (!IsValidPile(pile)) return kEmptyPile;
  const auto i = static_cast<std::size_t>(pile.index);
  switch (pile.kind) {
    case PileKind::Cell:
      return position_.cells[i];
    case PileKind::Foundation:
      return position_.foundations[i];
    case PileKind::Column:
      return position_.columns[i];
  }
  return kEmptyPile;
}

std::vector<Card>& FreeCell::MutablePile(PileId pile) {
  // The usual trick to avoid writing `Pile()` twice: call the const version
  // through a const view of `*this`, then cast the const away. That is safe
  // because `*this` really is non-const here, and `pile` was already
  // checked, so this never returns `kEmptyPile` (which is truly const).
  return const_cast<std::vector<Card>&>(std::as_const(*this).Pile(pile));
}

}  // namespace freecell
