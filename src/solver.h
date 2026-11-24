#ifndef FREECELL_SOLVER_H_
#define FREECELL_SOLVER_H_

#include <atomic>
#include <cstdint>
#include <vector>

#include "freecell.h"

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief How a solve ended.
enum class SolveStatus : std::uint8_t {
  /// @brief A winning line was found.
  Solved,
  /// @brief Every reachable position was tried and none wins.
  Unsolvable,
  /// @brief The search hit its size limit before it could decide.
  GaveUp,
  /// @brief The caller asked the search to stop.
  Cancelled,
};

/// @brief The answer from `Solve()`.
struct SolveResult {
  /// @brief How the search ended.
  SolveStatus status = SolveStatus::GaveUp;
  /// @brief The winning line when `status` is `Solved`: player moves only.
  /// After each move (and once before the first one), play every
  /// `FreeCell::NextSafeAutoMove()` until there are none.
  std::vector<Move> moves;
  /// @brief Positions the search looked at.
  int positions = 0;
};

/// @brief Searches for a way to win from `start`.
///
/// It is a best-first search: it keeps a queue of positions to look at,
/// always takes the one that looks closest to a win (most cards up, the
/// next cards needed nearest the top, free cells and columns open), and
/// adds every position one move away from it. A hash of each position it
/// has seen stops it from looking at the same position twice, and treating
/// columns and cells as unordered lets it skip mirror images.
///
/// The search only uses single-card moves, which is all the rules allow;
/// run moves in the game are a shortcut for several of them.
///
/// It runs safely on another thread: it only reads `start` (a copy is
/// fine) and `cancel`, and shares nothing else.
/// @param start Position to solve from.
/// @param max_positions Search size limit; past it the answer is `GaveUp`.
/// @param cancel Optional flag; when another thread sets it to `true`, the
///        search stops soon after with `Cancelled`. `nullptr` means "never".
/// @return The result.
SolveResult Solve(const Position& start, int max_positions,
                  const std::atomic<bool>* cancel = nullptr);

}  // namespace freecell

#endif  // FREECELL_SOLVER_H_
