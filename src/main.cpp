#include <raylib.h>

#include "game.h"
#include "screen.h"

/// @brief Opens the window and audio device, runs the game loop, and cleans
/// up raylib resources in reverse initialization order.
/// @return 0 on a normal exit.
int main() {
  // Ask for 4x multisample anti-aliasing (smooth rounded corners) and a
  // full-resolution framebuffer on high-DPI ("Retina") screens, so the card
  // art stays sharp. raylib keeps window and mouse coordinates in logical
  // pixels, so the rest of the code doesn't change.
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
  InitWindow(freecell::kScreenWidth, freecell::kScreenHeight, "FreeCell");
  // The audio device must be open before `Game` builds its sounds. If it
  // fails (no sound card), the game still runs, just silently.
  InitAudioDevice();
  SetTargetFPS(60);
  // Esc closes the "choose a game" box, so it must not also close the
  // window. `KEY_NULL` turns off raylib's built-in exit key; the Game
  // reports when Esc should quit instead.
  SetExitKey(KEY_NULL);

  // Explicit inner scope (RAII): `game` is destroyed at the closing brace, so
  // its destructor saves records and frees textures and sounds while raylib
  // is still initialized, before CloseAudioDevice() and CloseWindow() tear
  // down the underlying backends.
  {
    freecell::Game game;
    // The window's close button, or Esc outside the "choose a game" box,
    // ends the loop.
    while (!WindowShouldClose() && !game.quit_requested()) {
      game.Update(GetFrameTime());

      BeginDrawing();
      game.Draw();
      EndDrawing();
    }
  }

  CloseAudioDevice();
  CloseWindow();
  return 0;
}
