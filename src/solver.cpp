#include "solver.h"

#include <algorithm>
#include <array>
#include <functional>
#include <queue>
#include <unordered_set>
#include <utility>

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief The compact position and search machinery internal to
/// `solver.cpp`.
namespace {

/// @brief Most cards a column can hold during the search. A dealt column
/// has at most 7 cards, and at most 12 more (Queen down to Ace) can be
/// built on top, so 24 leaves room to spare.
constexpr int kMaxHeight = 24;
/// @brief Marks an empty free cell.
constexpr std::uint8_t kNoCard = 0xFF;
/// @brief How often (in positions) the search checks the cancel flag.
constexpr int kCancelCheckInterval = 512;

/// @brief Returns the rank (1..13) of a card number from `CardId()`.
/// @param id Card number (0..51).
/// @return Rank.
constexpr int RankOf(std::uint8_t id) { return id % kRankCount + 1; }

/// @brief Returns the suit index (0..3) of a card number.
/// @param id Card number (0..51).
/// @return Suit index.
constexpr int SuitOf(std::uint8_t id) { return id / kRankCount; }

/// @brief Returns true for a red card number.
/// @param id Card number (0..51).
/// @return True for diamonds and hearts.
constexpr bool RedOf(std::uint8_t id) {
  return IsRed(static_cast<Suit>(SuitOf(id)));
}

/// @brief A position packed into fixed-size arrays of card numbers.
///
/// The game's `Position` uses `std::vector`s, which allocate memory. The
/// search makes hundreds of thousands of positions, so it uses this
/// allocation-free copy instead: copying one is a single block copy.
struct State {
  /// @brief Card numbers in each column, bottom first.
  std::array<std::array<std::uint8_t, kMaxHeight>, kColumnCount> columns{};
  /// @brief Cards in each column.
  std::array<std::uint8_t, kColumnCount> heights{};
  /// @brief Card number in each free cell, or `kNoCard`.
  std::array<std::uint8_t, kCellCount> cells{};
  /// @brief Cards on each foundation (indexed by suit).
  std::array<std::uint8_t, kFoundationCount> founded{};
};

/// @brief One position the search has reached.
struct Node {
  /// @brief The position.
  State state;
  /// @brief Index of the node it was reached from (`-1` for the start).
  int parent = -1;
  /// @brief The player move that led here from the parent.
  Move move;
};

/// @brief Returns true if card `id` can go on its foundation and nothing
/// could still need it in a column (the same rule as `IsSafeToFound()`).
/// @param s Position.
/// @param id Card number.
/// @return True if it should go up by itself.
bool SafeToFound(const State& s, std::uint8_t id) {
  const int rank = RankOf(id);
  const int suit = SuitOf(id);
  if (s.founded[static_cast<std::size_t>(suit)] != rank - 1) return false;
  if (rank <= 2) return true;
  for (int other = 0; other < kSuitCount; ++other) {
    if (IsRed(static_cast<Suit>(other)) == RedOf(id)) continue;
    if (s.founded[static_cast<std::size_t>(other)] < rank - 1) return false;
  }
  return true;
}

/// @brief Moves every safe card up to its foundation, repeatedly, until
/// none is left. The game does the same after each move, so the search
/// and the game always agree on the position.
/// @param s Position, changed in place.
void AutoFound(State& s) {
  bool moved = true;
  while (moved) {
    moved = false;
    for (std::uint8_t& cell : s.cells) {
      if (cell != kNoCard && SafeToFound(s, cell)) {
        ++s.founded[static_cast<std::size_t>(SuitOf(cell))];
        cell = kNoCard;
        moved = true;
      }
    }
    for (int c = 0; c < kColumnCount; ++c) {
      const auto ci = static_cast<std::size_t>(c);
      while (s.heights[ci] > 0) {
        const std::uint8_t top = s.columns[ci][s.heights[ci] - 1U];
        if (!SafeToFound(s, top)) break;
        ++s.founded[static_cast<std::size_t>(SuitOf(top))];
        --s.heights[ci];
        moved = true;
      }
    }
  }
}

/// @brief Returns true if every card is on a foundation.
/// @param s Position.
/// @return True for a win.
bool IsWin(const State& s) {
  return std::ranges::all_of(s.founded,
                             [](std::uint8_t n) { return n == kRankCount; });
}

/// @brief Scores how far a position looks from a win. Lower is better.
///
/// The search always explores the lowest score next. The score is a guess
/// (a "heuristic"), so the search is fast but its winning lines are not
/// the shortest possible.
/// @param s Position.
/// @return Score.
int Heuristic(const State& s) {
  int score = 0;
  int founded = 0;
  for (const std::uint8_t n : s.founded) founded += n;
  score += (kDeckSize - founded) * 6;
  // The next card each foundation needs: every card on top of it is in the
  // way.
  for (int suit = 0; suit < kSuitCount; ++suit) {
    const int next = s.founded[static_cast<std::size_t>(suit)];
    if (next >= kRankCount) continue;
    const auto wanted = static_cast<std::uint8_t>(suit * kRankCount + next);
    for (int c = 0; c < kColumnCount; ++c) {
      const auto ci = static_cast<std::size_t>(c);
      for (int i = 0; i < s.heights[ci]; ++i) {
        if (s.columns[ci][static_cast<std::size_t>(i)] == wanted) {
          score += (s.heights[ci] - 1 - i) * 2;
        }
      }
    }
  }
  for (const std::uint8_t cell : s.cells) {
    if (cell != kNoCard) score += 3;
  }
  for (int c = 0; c < kColumnCount; ++c) {
    const auto ci = static_cast<std::size_t>(c);
    if (s.heights[ci] == 0) {
      score -= 6;
      continue;
    }
    // Each place where a card is not on the next card up in the other
    // colour is a tangle someone has to sort out later.
    for (int i = 1; i < s.heights[ci]; ++i) {
      const std::uint8_t below = s.columns[ci][static_cast<std::size_t>(i - 1)];
      const std::uint8_t above = s.columns[ci][static_cast<std::size_t>(i)];
      if (RankOf(above) != RankOf(below) - 1 || RedOf(above) == RedOf(below)) {
        score += 1;
      }
    }
  }
  return score;
}

/// @brief Mixes bytes into a 64-bit FNV-1a hash.
/// @param hash Running hash value, changed in place.
/// @param byte Next byte.
void Mix(std::uint64_t& hash, std::uint8_t byte) {
  hash ^= byte;
  hash *= 1099511628211ULL;  // The 64-bit FNV prime.
}

/// @brief Hashes a position so that positions that differ only in the order
/// of their columns or cells get the same hash: for winning, those are the
/// same position.
/// @param s Position.
/// @return 64-bit hash.
std::uint64_t CanonicalHash(const State& s) {
  std::uint64_t hash = 14695981039346656037ULL;  // The FNV offset basis.
  for (const std::uint8_t n : s.founded) Mix(hash, n);
  std::array<std::uint8_t, kCellCount> cells = s.cells;
  std::ranges::sort(cells);
  for (const std::uint8_t cell : cells) Mix(hash, cell);
  // Sort column numbers by their contents, then hash in that order.
  std::array<int, kColumnCount> order{};
  for (int c = 0; c < kColumnCount; ++c) order[static_cast<std::size_t>(c)] = c;
  // The lambda compares two columns as byte strings. It captures `s` by
  // reference; that is safe because the sort finishes before `s` can go.
  std::ranges::sort(order, [&s](int a, int b) {
    const auto ai = static_cast<std::size_t>(a);
    const auto bi = static_cast<std::size_t>(b);
    return std::lexicographical_compare(
        s.columns[ai].begin(), s.columns[ai].begin() + s.heights[ai],
        s.columns[bi].begin(), s.columns[bi].begin() + s.heights[bi]);
  });
  for (const int c : order) {
    const auto ci = static_cast<std::size_t>(c);
    Mix(hash, s.heights[ci]);
    for (int i = 0; i < s.heights[ci]; ++i) {
      Mix(hash, s.columns[ci][static_cast<std::size_t>(i)]);
    }
    Mix(hash, kNoCard);  // Separator, so [1,2][3] and [1][2,3] differ.
  }
  return hash;
}

/// @brief Packs a game Position into a State.
/// @param position Position to pack.
/// @param out Where to write the result.
/// @return False if a column is too tall to pack (never happens in play).
bool Pack(const Position& position, State& out) {
  for (int c = 0; c < kColumnCount; ++c) {
    const auto& column = position.columns[static_cast<std::size_t>(c)];
    if (column.size() > static_cast<std::size_t>(kMaxHeight)) return false;
    out.heights[static_cast<std::size_t>(c)] =
        static_cast<std::uint8_t>(column.size());
    for (std::size_t i = 0; i < column.size(); ++i) {
      out.columns[static_cast<std::size_t>(c)][i] =
          static_cast<std::uint8_t>(CardId(column[i]));
    }
  }
  for (int c = 0; c < kCellCount; ++c) {
    const auto& cell = position.cells[static_cast<std::size_t>(c)];
    out.cells[static_cast<std::size_t>(c)] =
        cell.empty() ? kNoCard : static_cast<std::uint8_t>(CardId(cell.back()));
  }
  for (int f = 0; f < kFoundationCount; ++f) {
    out.founded[static_cast<std::size_t>(f)] = static_cast<std::uint8_t>(
        position.foundations[static_cast<std::size_t>(f)].size());
  }
  return true;
}

/// @brief Returns true if card `id` can go on a column whose top is `top`.
/// @param id Card to place.
/// @param top Card number on top of the column.
/// @return True for one rank lower in the other colour.
bool FitsOn(std::uint8_t id, std::uint8_t top) {
  return RankOf(id) == RankOf(top) - 1 && RedOf(id) != RedOf(top);
}

/// @brief Calls `visit(move, child)` for every single-card move from `s`.
/// Each child has had its safe foundation moves played already.
///
/// A template parameter `Visit` lets the caller pass any callable (here, a
/// lambda) with no function-pointer overhead. `Visit&&` is a "forwarding
/// reference": it binds to whatever the caller passes without copying it.
/// @tparam Visit Callable taking `(const Move&, const State&)`.
/// @param s Position to move from.
/// @param visit Called once per move.
template <typename Visit>
void ForEachMove(const State& s, Visit&& visit) {
  // `[&]` captures `s` and `visit` by reference; this lambda never outlives
  // the call.
  auto emit = [&](const Move& move, State child) {
    AutoFound(child);
    visit(move, child);
  };
  int first_empty_column = -1;
  for (int c = 0; c < kColumnCount; ++c) {
    if (s.heights[static_cast<std::size_t>(c)] == 0) {
      first_empty_column = c;
      break;
    }
  }
  int first_empty_cell = -1;
  for (int c = 0; c < kCellCount; ++c) {
    if (s.cells[static_cast<std::size_t>(c)] == kNoCard) {
      first_empty_cell = c;
      break;
    }
  }

  // Moves out of free cells.
  for (int cell = 0; cell < kCellCount; ++cell) {
    const std::uint8_t id = s.cells[static_cast<std::size_t>(cell)];
    if (id == kNoCard) continue;
    const PileId from{PileKind::Cell, cell};
    if (s.founded[static_cast<std::size_t>(SuitOf(id))] == RankOf(id) - 1) {
      State child = s;
      child.cells[static_cast<std::size_t>(cell)] = kNoCard;
      ++child.founded[static_cast<std::size_t>(SuitOf(id))];
      emit({from, 0, {PileKind::Foundation, SuitOf(id)}}, child);
    }
    for (int c = 0; c < kColumnCount; ++c) {
      const auto ci = static_cast<std::size_t>(c);
      const int h = s.heights[ci];
      // All empty columns are alike, so only try the first one.
      if (h == 0
              ? c != first_empty_column
              : !FitsOn(id, s.columns[ci][static_cast<std::size_t>(h - 1)])) {
        continue;
      }
      State child = s;
      child.cells[static_cast<std::size_t>(cell)] = kNoCard;
      child.columns[ci][static_cast<std::size_t>(h)] = id;
      ++child.heights[ci];
      emit({from, 0, {PileKind::Column, c}}, child);
    }
  }

  // Moves of a column's top card.
  for (int src = 0; src < kColumnCount; ++src) {
    const auto si = static_cast<std::size_t>(src);
    const int h = s.heights[si];
    if (h == 0) continue;
    const std::uint8_t id = s.columns[si][static_cast<std::size_t>(h - 1)];
    const PileId from{PileKind::Column, src};
    if (s.founded[static_cast<std::size_t>(SuitOf(id))] == RankOf(id) - 1) {
      State child = s;
      --child.heights[si];
      ++child.founded[static_cast<std::size_t>(SuitOf(id))];
      emit({from, h - 1, {PileKind::Foundation, SuitOf(id)}}, child);
    }
    for (int dst = 0; dst < kColumnCount; ++dst) {
      if (dst == src) continue;
      const auto di = static_cast<std::size_t>(dst);
      const int dh = s.heights[di];
      if (dh == 0) {
        // Moving a column's only card to an empty column changes nothing.
        if (dst != first_empty_column || h == 1) continue;
      } else if (!FitsOn(id, s.columns[di][static_cast<std::size_t>(dh - 1)])) {
        continue;
      }
      State child = s;
      --child.heights[si];
      child.columns[di][static_cast<std::size_t>(dh)] = id;
      ++child.heights[di];
      emit({from, h - 1, {PileKind::Column, dst}}, child);
    }
    if (first_empty_cell >= 0) {
      State child = s;
      --child.heights[si];
      child.cells[static_cast<std::size_t>(first_empty_cell)] = id;
      emit({from, h - 1, {PileKind::Cell, first_empty_cell}}, child);
    }
  }
}

}  // namespace

SolveResult Solve(const Position& start, int max_positions,
                  const std::atomic<bool>* cancel) {
  SolveResult result;
  State root;
  if (!Pack(start, root)) return result;
  AutoFound(root);

  std::vector<Node> nodes;
  nodes.push_back({root, -1, {}});
  std::unordered_set<std::uint64_t> seen;
  seen.insert(CanonicalHash(root));

  // A min-priority queue of (score, node index): `std::priority_queue`
  // normally puts the *largest* element on top, so `std::greater<>` flips
  // it to give the smallest score first.
  using Entry = std::pair<int, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
  open.push({Heuristic(root), 0});

  int goal = IsWin(root) ? 0 : -1;
  int popped = 0;
  while (goal < 0 && !open.empty()) {
    if (++popped % kCancelCheckInterval == 0 && cancel != nullptr &&
        cancel->load()) {
      result.status = SolveStatus::Cancelled;
      result.positions = static_cast<int>(nodes.size());
      return result;
    }
    if (static_cast<int>(nodes.size()) >= max_positions) {
      result.status = SolveStatus::GaveUp;
      result.positions = static_cast<int>(nodes.size());
      return result;
    }
    const int current = open.top().second;
    open.pop();
    // Copy the state out: `nodes.push_back` below may move every node to a
    // bigger buffer, which would leave a reference into `nodes` dangling.
    const State state = nodes[static_cast<std::size_t>(current)].state;
    ForEachMove(state, [&](const Move& move, const State& child) {
      if (goal >= 0) return;
      // `insert` returns a pair; `.second` is false if the hash was already
      // in the set, meaning this position was seen before.
      if (!seen.insert(CanonicalHash(child)).second) return;
      nodes.push_back({child, current, move});
      const int index = static_cast<int>(nodes.size()) - 1;
      if (IsWin(child)) {
        goal = index;
        return;
      }
      open.push({Heuristic(child), index});
    });
  }

  result.positions = static_cast<int>(nodes.size());
  if (goal < 0) {
    result.status = SolveStatus::Unsolvable;
    return result;
  }
  // Walk back from the win to the start, then reverse to get the line in
  // playing order.
  for (int n = goal; nodes[static_cast<std::size_t>(n)].parent >= 0;
       n = nodes[static_cast<std::size_t>(n)].parent) {
    result.moves.push_back(nodes[static_cast<std::size_t>(n)].move);
  }
  std::ranges::reverse(result.moves);
  result.status = SolveStatus::Solved;
  return result;
}

}  // namespace freecell
