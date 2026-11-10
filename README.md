# HexTacToe

HexTacToe is a game played on an infinite hexagonal grid. The two players are blue and red. Each player colours tiles until 6 in a row is achieved. 

On each turn, a player colours any two tiles their colour. The exception is the first turn, where only one tile is coloured. Thus, the first player colours one tile, the second colours two, the first colours two, and so on.

Full disclosure, this was vibe coded by Claude Opus 5.5 in about 15 minutes. 

## Controls

| Action | Mouse / trackpad | Keyboard |
| --- | --- | --- |
| Colour a hex | Left click | |
| Move the board | Drag (any button) | Arrow keys / WASD |
| Zoom | Scroll wheel, pinch | `+` / `-` |
| Undo | **Undo** button | `U`, `Backspace`, `Ctrl/Cmd+Z` |
| New game | **New game** button | `N` |
| Opponent (human / computer plays Red / computer plays Blue) | Opponent button | `M` |
| Centre on the tiles | | `C` / `Home` |
| Fullscreen | | `F` / `F11` |
| Show / hide help | | `H` |

## Download

Get the latest version from
[Releases](https://github.com/j-kepler/hextactoe/releases/latest).

- **macOS 11+** (Apple silicon and Intel): open `HexTacToe-macOS.dmg` and drag HexTacToe to Applications. The app isn't notarized, so macOS blocks it the first time: open System Settings → Privacy & Security and click **Open Anyway**.
- **Linux** (x86_64): extract `HexTacToe-Linux-x86_64.tar.gz` and run `./hextactoe`.
- Anything else: figure it out

## Building from source

Requires CMake and SDL3 (`brew install cmake sdl3` on macOS, `libsdl3-dev` or `SDL3-devel` on Linux). Without SDL3, CMake downloads and builds it.

```bash
cmake -S . -B build
cmake --build build
```

Then open `build/HexTacToe.app` (macOS) or run `build/hextactoe` (Linux).

## License

[GPL-3.0](LICENSE)

The text is set in Latin Modern Roman (the OpenType version of Computer Modern) by B. Jackowski and J. M. Nowacki, under the
[GUST Font License](https://www.gust.org.pl/projects/e-foundry/licenses).
