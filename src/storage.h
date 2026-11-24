#ifndef FREECELL_STORAGE_H_
#define FREECELL_STORAGE_H_

#include <filesystem>

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Number of card back designs the player can choose from.
inline constexpr int kCardBackCount = 4;

/// @brief Everything persisted between runs.
struct SaveData {
  /// @brief Chosen card back design (`0..kCardBackCount-1`).
  int card_back = 0;
  /// @brief True if the player turned the sound off.
  bool muted = false;
  /// @brief Games won or given up after at least one move.
  int games = 0;
  /// @brief Games won.
  int wins = 0;
  /// @brief Fastest win in seconds (`0.0` = no win yet).
  float best_time = 0.0f;
  /// @brief Fewest moves in a win (`0` = no win yet).
  int best_moves = 0;
  /// @brief Wins in a row right now.
  int streak = 0;
  /// @brief Longest run of wins in a row.
  int best_streak = 0;
};

/// @brief What one finished (or abandoned) game achieved.
struct GameResult {
  /// @brief True for a win, false for a game given up.
  bool won = false;
  /// @brief Seconds played.
  float seconds = 0.0f;
  /// @brief Moves made.
  int moves = 0;
};

/// @brief Returns the default save file path (`~/.freecell.cfg`).
/// @return `$HOME/.freecell.cfg`, or `.freecell.cfg` in the working
///         directory if `HOME` is unset.
std::filesystem::path DefaultSavePath();

/// @brief Loads SaveData from `path`. Missing or malformed fields fall back
/// to defaults without failing or throwing.
/// @param path File path to read.
/// @return Loaded SaveData (or defaults if the file does not exist yet).
SaveData LoadSaveData(const std::filesystem::path& path);

/// @brief Writes `data` to `path` in a human-readable `key=value` format.
/// @param path Destination file path (created or overwritten).
/// @param data Settings and records to persist.
/// @return True if the file was written cleanly.
bool WriteSaveData(const std::filesystem::path& path, const SaveData& data);

/// @brief Records a finished or abandoned game.
/// @param data SaveData modified in place (passed by non-const `&`).
/// @param result What the game achieved.
/// @return True if a win set a new best time.
bool RecordResult(SaveData& data, const GameResult& result);

}  // namespace freecell

#endif  // FREECELL_STORAGE_H_
