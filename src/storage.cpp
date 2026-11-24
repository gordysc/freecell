#include "storage.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>

/// @brief Everything that belongs to the FreeCell game.
namespace freecell {

/// @brief Defensive parsing helpers internal to `storage.cpp`.
namespace {

/// @brief Largest count the loader accepts.
constexpr int kMaxStored = 999999999;

/// @brief Parses an integer from `value` and clamps it to `[min_val, max_val]`.
///
/// `std::stoi` throws `std::invalid_argument` or `std::out_of_range` on bad
/// input; catching all exceptions guarantees a hand-edited or corrupted save
/// file can never crash startup.
///
/// @param value Raw string slice after `=`.
/// @param fallback Value returned if parsing fails.
/// @param min_val Minimum permitted value.
/// @param max_val Maximum permitted value.
/// @return Clamped integer result or `fallback`.
int ParseInt(const std::string& value, int fallback, int min_val, int max_val) {
  try {
    return std::clamp(std::stoi(value), min_val, max_val);
  } catch (...) {
    return fallback;
  }
}

/// @brief Parses a floating-point number from `value` and clamps it to
/// `[min_val, max_val]`, returning `fallback` on malformed text.
/// @param value Raw string slice after `=`.
/// @param fallback Value returned if `std::stof` throws.
/// @param min_val Minimum permitted value.
/// @param max_val Maximum permitted value.
/// @return Clamped float result or `fallback`.
float ParseFloat(const std::string& value, float fallback, float min_val,
                 float max_val) {
  try {
    return std::clamp(std::stof(value), min_val, max_val);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

std::filesystem::path DefaultSavePath() {
  // `if (init; cond)` (C++17) limits `home`'s scope to this `if` statement.
  // `std::getenv` returns a non-owning pointer into the process environment
  // block (never free it), or `nullptr` if HOME is unset.
  if (const char* home = std::getenv("HOME");
      home != nullptr && *home != '\0') {
    // `operator/` on `std::filesystem::path` joins two path pieces with the
    // platform's directory separator.
    return std::filesystem::path(home) / ".freecell.cfg";
  }
  return std::filesystem::path(".freecell.cfg");
}

SaveData LoadSaveData(const std::filesystem::path& path) {
  SaveData data;
  // RAII: `std::ifstream` opens the file in its constructor and automatically
  // closes the file handle in its destructor when this function returns.
  std::ifstream input(path);
  if (!input.is_open()) return data;

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);

    if (key == "card_back") {
      data.card_back = ParseInt(val, 0, 0, kCardBackCount - 1);
    } else if (key == "muted") {
      data.muted = ParseInt(val, 0, 0, 1) == 1;
    } else if (key == "games") {
      data.games = ParseInt(val, 0, 0, kMaxStored);
    } else if (key == "wins") {
      data.wins = ParseInt(val, 0, 0, kMaxStored);
    } else if (key == "best_time") {
      data.best_time = ParseFloat(val, 0.0f, 0.0f, 999999.0f);
    } else if (key == "best_moves") {
      data.best_moves = ParseInt(val, 0, 0, kMaxStored);
    } else if (key == "streak") {
      data.streak = ParseInt(val, 0, 0, kMaxStored);
    } else if (key == "best_streak") {
      data.best_streak = ParseInt(val, 0, 0, kMaxStored);
    }
  }
  return data;
}

bool WriteSaveData(const std::filesystem::path& path, const SaveData& data) {
  // `std::ofstream` truncates (empties) an existing file on open by default.
  std::ofstream output(path);
  if (!output.is_open()) return false;
  output << "# FreeCell save file\n";
  output << "card_back=" << data.card_back << "\n";
  output << "muted=" << (data.muted ? 1 : 0) << "\n";
  output << "games=" << data.games << "\n";
  output << "wins=" << data.wins << "\n";
  // `std::fixed` + `std::setprecision(3)` print floats as `12.345` instead
  // of switching to scientific notation for very small or large values.
  output << std::fixed << std::setprecision(3);
  output << "best_time=" << data.best_time << "\n";
  output << "best_moves=" << data.best_moves << "\n";
  output << "streak=" << data.streak << "\n";
  output << "best_streak=" << data.best_streak << "\n";
  // `good()` is false if any write above failed (disk full, permissions).
  return output.good();
}

bool RecordResult(SaveData& data, const GameResult& result) {
  ++data.games;
  if (!result.won) {
    data.streak = 0;
    return false;
  }
  ++data.wins;
  ++data.streak;
  data.best_streak = std::max(data.best_streak, data.streak);
  if (data.best_moves == 0 || result.moves < data.best_moves) {
    data.best_moves = result.moves;
  }
  if (data.best_time <= 0.0f || result.seconds < data.best_time) {
    data.best_time = result.seconds;
    return true;
  }
  return false;
}

}  // namespace freecell
