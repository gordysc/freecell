# FreeCell

This is FreeCell solitaire in **C++20** with [raylib](https://www.raylib.com/). A pure C++20 core library (`freecell_core`) holds the rules and a solver. The card art comes from the public domain [Public Domain Playing Cards](https://github.com/AustinGabriel/Public-Domain-and-CC0-Playing-Cards) deck. The game synthesizes all of its sounds in code.

## Highlights

- **FreeCell rules**: The game deals all 52 cards face up into eight columns. Each of the four free cells holds one card. Build the foundations up by suit from Ace to King. Build the columns down in alternate colours. Any card can go on an empty column.
- **Run moves**: Drag a run of cards in one move. The game allows `(free cells + 1) × 2^(empty columns)` cards, as if it moved them one by one through the free space. The bar shows how many cards you can move now. If a run is too long, a message tells you the limit.
- **Numbered deals**: Each game has a number. Numbers 1 to 32000 use the classic numbered-deal shuffle, so they match the well-known lists of numbered deals. Click **GAME #** or push `G` to play a deal by its number. **RESTART** plays the same deal again.
- **Automatic foundation moves**: A card goes up by itself when no other card can still need it.
- **Solver hints**: **HINT** runs a solver on a background thread. The solver shows the next move of a winning line, and tells you if the position can no longer be won. In a test with a limit of 200,000 positions, the solver won 991 of the first 1000 deals. The game uses a limit of 300,000. The solver also proves that deal 11982, the one classic deal with no solution, cannot be won.
- **Drag and drop or click**: Drag cards to a pile, or click a card to send it to the best legal pile.
- **Undo**: Take back any number of moves. One undo also takes back the automatic moves that followed.
- **Animations**: The game deals and turns over the cards one by one and slides every move. A win sends every card into the air in a shower of confetti.
- **Records**: The game saves games played, wins, best time, fewest moves, and win streaks to `~/.freecell.cfg`. It also saves the card back and the sound setting.

## Build in CLion

1. Open this folder in CLion.
2. Make sure that the computer has an internet connection. On the first load, CMake downloads raylib 5.5 with `FetchContent`.
3. Select the `freecell` run configuration.
4. Push Run.

To run the core test suite, select the `freecell_tests` run configuration and push Run.

## Build and Test from the Command Line

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/freecell
```

Each build copies the `assets/` folder next to the executable. The game loads the card images from there.

## Controls

| Action | Mouse | Keyboard |
| --- | --- | --- |
| Move cards | Drag them to a pile | — |
| Send a card to its best pile | Click it | — |
| Undo | Click **UNDO** | `U`, `Z`, or `Backspace` |
| Show a hint | Click **HINT** | `H` |
| Start a new random game | Click **NEW GAME** | `N` or `F2` |
| Play this deal again | Click **RESTART** | `R` |
| Play a deal by number | Click **GAME #** | `G`, type the number, then `Enter` |
| Change the card back | Click **BACK** | `B` |
| Sound on or off | Click **SOUND** | `M` |
| Quit | Close the window | `Esc` |

A game that you leave after one or more moves counts as a game played. **RESTART** also counts the current game as played.

## How the Solver Works

The solver is a best-first search in `src/solver.cpp`:

1. The solver packs the position into small fixed-size arrays, so a copy needs no memory allocation.
2. The solver scores each position. Cards on the foundations make the score better. Cards that cover the next card for a foundation, full free cells, and columns out of order make the score worse.
3. The solver always looks at the position with the best score next.
4. The solver records a hash of each position that it sees. Positions that differ only in the order of columns or free cells get the same hash, so the solver looks at each position one time only.
5. If the solver tries every reachable position without a win, the position cannot be won. If the solver reaches its size limit first, the hint falls back to a simple legal move.

## Project Layout

- `src/card.*`, `src/freecell.*`, `src/solver.*`, `src/storage.*`: The pure C++20 core (`freecell_core`). It has no raylib dependency.
- `src/layout.*`, `src/theme.h`, `src/textures.*`, `src/effects.*`, `src/audio.*`, `src/renderer.*`, `src/game.*`, `src/main.cpp`: The raylib table layout, the colours, the card images, the card animations and win celebration, the synthesized sound, the draw code, and the game loop.
- `assets/cards/`: The 52 card faces and 4 card backs, scaled down to 360x504 from the original 1500x2100 images. `assets/cards/LICENSE` holds the CC0 public domain dedication of the art.
- `tests/freecell_tests.cpp`: The headless CTest suite. It tests the classic layout of deal 1, the move rules, run moves and their size limit, the automatic move rule, undo, click moves, a position with no moves, solver wins on deals 1 to 12 replayed through the rules, solver cancellation, and the save file.

## Credits

The card art is from [Public Domain Playing Cards](https://github.com/AustinGabriel/Public-Domain-and-CC0-Playing-Cards) by AustinGabriel, released under CC0 1.0. Credit is not required, but this project gives it with thanks.
