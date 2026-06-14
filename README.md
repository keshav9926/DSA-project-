# ChessMind ♟️
**A High-Performance Custom C++ Minimax Chess Engine & Real-time Cyberpunk Web Interface**

🔗 **Project Repository**: [GitHub - keshav9926/DSA-project-](https://github.com/keshav9926/DSA-project-)

ChessMind is an end-to-end, multi-tiered chess platform featuring a custom-built, highly optimized C++ chess engine communicating via the UCI protocol with a lightweight FastAPI/WebSocket bridge, rendered in a stunning cyberpunk retro-classic neon React dashboard. It includes real-time evaluation, automatic Stockfish suggestions, and difficulty levels.

---

## 🚀 Quick Start (2 Commands)

### 💻 Windows
1. Double-click **`setup.bat`** (installs Python dependencies, verifies `g++`, and compiles the engine).
2. Double-click **`run.bat`** (starts the WebSocket bridge and launches the client in your browser).

### 🐧 Linux / macOS
```bash
chmod +x setup.sh run.sh
./setup.sh        # Installs dependencies, compiles C++ engine, and runs Perft tests
./run.sh          # Starts the bridge server and launches the client
```

---

## 🎨 System Architecture

```mermaid
graph TD
    A[React Client / Webpage] <-->|WebSockets| B[FastAPI Python Bridge]
    B <-->|UCI Subprocess StdIO| C[Custom C++ ChessMind Engine]
    B <-->|UCI Subprocess StdIO| D[Stockfish Engine (Optional, for Hints)]
```

1. **Frontend (React + HTML/CSS)**: An interactive retro neon user interface featuring a Slate board, evaluation bars, real-time AI thinking HUD (Nodes, Speed, Depth, best line), and game history.
2. **WebSocket Bridge (Python/FastAPI)**: Serves as the orchestration layer. It manages game sessions, synchronizes board states, spawns the custom engine, and queries Stockfish for hints.
3. **C++ Engine**: The core decision-making entity. It parses FEN strings, generates legal moves, evaluates board states, and performs search optimizations.

---

## 🧠 Custom C++ Engine Inner Workings

The C++ engine is located in `/engine` and is built on standard chess programming principles:

### 1. Board Representation & Hashing (`board.h/cpp`, `types.h`)
- **Bitboards**: Represents the 64 squares of a chess board as 64-bit unsigned integers (`uint64_t`). Separate bitboards are maintained for piece types and colors.
- **Zobrist Hashing**: Encodes board states into a unique 64-bit signature. The signature is updated incrementally using XOR operations when moves are made or unmade, serving as a primary key for transposition tables.

### 2. Legal Move Generation (`movegen.h/cpp`)
- Employs a highly optimized move generator that evaluates pawn advances, knight jumps, sliding attacks (rooks, bishops, queens using magic bitboard logic concepts), and king steps.
- Captures and normal moves are generated, checked for legality (ensuring the king is not left in check), and tested against standard chess validation suites.
- **Perft Validation**: Includes an 11-test suite verifying the generator's mathematical correctness across standard and complex tactical positions.

### 3. Evaluation Function (`eval.h/cpp`)
- Converts chess positions into a numeric score (positive for White, negative for Black) using:
  - **Material Values**: Pawn (100), Knight (320), Bishop (330), Rook (500), Queen (900).
  - **Piece-Square Tables (PST)**: Encourages pieces to occupy active squares (e.g. Knights in the center, Pawns advancing, King castling safely).
  - **Positional Sub-heuristics**: Penalizes doubled/isolated pawns, awards bishop pairs, and computes piece mobility.

### 4. Search Subsystem (`search.h/cpp`)
- **Iterative Deepening**: Searches at incrementing depths (1, 2, 3...) to ensure a best-move estimate is always available, managing search times effectively.
- **Alpha-Beta Pruning**: Reduces the exponential game tree by discarding subtrees that are worse than previously evaluated moves.
- **Transposition Tables (TT)**: A cache storing previous searches (depth, score, flag, best move) to prevent redundant work when encountering transposition states (different move orders leading to the same board).
- **Move Ordering**: Sorts moves to cause alpha-beta cutoffs as early as possible:
  1. *PV Move*: Explores the principal variation move from the previous search depth first.
  2. *MVV-LVA*: Most Valuable Victim - Least Valuable Aggressor (prioritizes captures like pawn takes queen).
  3. *Killer Moves*: Prioritizes quiet moves that caused cutoffs at the same depth in sibling branches.
  4. *History Heuristic*: Prioritizes moves that historically caused cutoffs throughout the search.
- **Quiescence Search**: Extends search depth to resolve tactical captures and checks, preventing the "horizon effect".

---

## 🌟 Advanced Features

### 1. Castling Implementation
- Supports castling for both White and Black. The board validates castling rights (from the FEN string), verifies path safety (squares must be empty and not under attack), and coordinates the king-rook swap.

### 2. Continuous Stockfish Auto-Hints
- The client features an **Auto Hints** toggle button that glows neon green when active. 
- When toggled **ON**, the WebSocket client requests Stockfish's top recommended lines automatically on every move. This eliminates manual clicking, providing a continuous real-time analysis overlay.

### 3. Engine Statistics HUD
- Shows real-time search metrics during the AI's turn:
  - **Depth**: Reached search depth (e.g., Depth 8).
  - **Score**: Current evaluation score in pawns (e.g., +1.20).
  - **Nodes**: Total positions visited (e.g., 2,500,000 nodes).
  - **Speed**: Search speed in Kilonodes per second (kN/s).
  - **Calculated Line**: The best variation (PV) the C++ engine predicts.

---

## 📂 Project Directory Structure

```
chessmind/
├── setup.bat/sh      ← Compiles engine, installs dependencies
├── run.bat/sh        ← Starts server bridge and opens client
├── benchmark.py      ← Validates search complexity and speed
├── engine/
│   ├── types.h       ← Type aliases and bitboard helpers
│   ├── board.h/cpp   ← FEN parsing, Zobrist keys, board mutations
│   ├── movegen.h/cpp ← Legal move generation algorithms
│   ├── eval.h/cpp    ← Positional and material valuation
│   ├── search.h/cpp  ← Minimax search, TT caching, move sorting
│   ├── book.h        ← Opening book database
│   ├── perft.cpp     ← Move generator validation test suite
│   ├── main.cpp      ← UCI protocol loops & Win32Thread pool
│   └── Makefile      ← Standard compiler Makefile
├── bridge/
│   ├── server.py     ← FastAPI WebSocket bridge
│   ├── chessmind.exe ← Engine production binary
│   └── stockfish.exe ← Stockfish binary (optional, for hints)
└── frontend/
    └── index.html    ← React + Vanilla CSS retro neon client
```

---

## 📊 Complexity Analysis Summary

- **Search Time Complexity**: $O(b^d)$ (where $b \approx 35$ is the branching factor, $d$ is depth). With Alpha-Beta pruning and optimized move ordering, this is reduced to approximately $O(b^{3d/4})$ or $O(b^{d/2})$ under optimal conditions.
- **Transposition Cache Space**: $O(N)$ space complexity, bounded by the maximum transposition table size (e.g., $1,048,576$ entries, occupying $\approx 32$ MB of memory).
- **Move Generator Space**: $O(1)$ auxiliary space during search, utilizing pre-allocated stack frames.
