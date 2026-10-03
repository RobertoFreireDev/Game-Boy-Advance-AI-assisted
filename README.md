# Game Boy Advance games, built 100% by AI

You describe the game; the AI writes everything (art, music, levels, C code, tools).
You only look and play.

| To… | Double-click |
|-----|--------------|
| see everything the AI made (read-only) | `view.bat` |
| build the game ROM | `build.bat` |
| play it in the mGBA emulator | `run.bat` |

**`main` is an empty game template**: one start screen that says "NEW GAME". Ask the AI to make
your game. Finished games live on their own branches, e.g. `games/night_swarm` (a survivors-like).
With mGBA's default keys: arrows = D-pad, **X** = A, **Z** = B.

How it all works — the node format, the engine and the rules the AI follows — is in
[`CLAUDE.md`](CLAUDE.md). Installing the tools is in [`SETUP.md`](SETUP.md).
