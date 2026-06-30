# Number Matching Game (Numberlink-style Puzzle) — ANSI C

A console-based **Number Matching / Flow puzzle game** written in pure ANSI C (C90), developed as the term project for **BLM1031 – Structural Programming** at Yıldız Technical University.

The game places each number `1..N` exactly twice on an `N x N` matrix — either randomly generated or loaded from a file — and challenges the player to connect every pair with a non-crossing path until the entire board is filled. It supports manual play, an automatic backtracking solver, undo/redo, save/resume, and a persistent scoreboard.

## Features

- **Random board generation** — generates a guaranteed-solvable `N x N` board (N between 3 and 10) using randomized color partitioning.
- **Load board from file** — reads a custom puzzle from a text file and validates that every number appears exactly twice before starting.
- **Manual play mode** — connect matching numbers cell by cell with full move validation.
- **Automatic solver mode** — a recursive DFS/backtracking algorithm that solves the board on its own and reports the number of backtracks used.
- **Undo / Redo system** — a doubly linked list of moves tracks every cell change, allowing the player to step backward and forward through their moves.
- **Save / Resume game** — the current board state and move progress can be saved to a file and resumed later.
- **Scoreboard** — completed games are scored (based on time, mode, board size, and undo count) and stored persistently; scores can be viewed from the main menu.
- **Solution verification** — checks that every color forms a single, non-branching, non-crossing path between its two endpoints and that no cell is left empty.

## Project Constraints

This project was implemented under a strict set of rules required by the course:

- Written entirely in **ANSI C (C90)** — compiles with `gcc -ansi -pedantic`.
- **No global or static variables.**
- **No `continue` and no `goto`** statements.
- `break` is used **only** inside `switch` statements.
- **Dynamic memory allocation** is used for every matrix and list (no fixed-size global arrays).
- Naming convention: `lowerCamelCase` for variables/functions, `UPPERCASE` for macros/constants.
- Every function is preceded by a comment describing its inputs and outputs.

## Build & Run

```bash
gcc -ansi -pedantic -o numbergame main.c
./numbergame
```

## How to Play

On launch, the main menu offers the following options:

1. **Create Random Matrix** — choose a board size N (3–10) and a solvable board is generated automatically.
2. **Create Matrix from File** — load a board layout from a text file.
3. **Show User Scores** — view the persistent scoreboard.
4. **Resume Saved Game** — continue a previously saved manual game.
5. **Exit**

Once a board is created or loaded, you can play in **Manual Mode** (entering source and destination coordinates to extend a path) or **Automatic Mode** (letting the solver complete the board for you). In manual mode you can undo/redo moves, save your progress, or quit at any time.

## File Structure

```
.
├── main.c        # Full source code (single-file ANSI C program)
└── README.md
```

## Author

**Abdullah Yaren** — Computer Engineering, Yıldız Technical University
Term project for BLM1031 — Structural Programming
