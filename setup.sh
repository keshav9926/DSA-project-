
#!/bin/bash
set -e

# ─── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

ok()   { echo -e "${GREEN}✓${NC} $1"; }
fail() { echo -e "${RED}✗ ERROR:${NC} $1"; exit 1; }
info() { echo -e "${CYAN}→${NC} $1"; }
warn() { echo -e "${YELLOW}⚠${NC} $1"; }
head() { echo -e "\n${BOLD}${BLUE}$1${NC}"; }

echo -e "${BOLD}"
echo "  ██████╗██╗  ██╗███████╗███████╗███████╗███╗   ███╗██╗███╗   ██╗██████╗ "
echo " ██╔════╝██║  ██║██╔════╝██╔════╝██╔════╝████╗ ████║██║████╗  ██║██╔══██╗"
echo " ██║     ███████║█████╗  ███████╗███████╗██╔████╔██║██║██╔██╗ ██║██║  ██║"
echo " ██║     ██╔══██║██╔══╝  ╚════██║╚════██║██║╚██╔╝██║██║██║╚██╗██║██║  ██║"
echo " ╚██████╗██║  ██║███████╗███████║███████║██║ ╚═╝ ██║██║██║ ╚████║██████╔╝"
echo "  ╚═════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝╚═╝     ╚═╝╚═╝╚═╝  ╚═══╝╚═════╝ "
echo -e "${NC}"
echo "  Chess engine with Minimax + Alpha-Beta pruning"
echo "  Human vs AI · Stockfish hints · Evaluation bar"
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ─── Step 1: Check prerequisites ──────────────────────────────────────────────
head "Step 1 — Checking prerequisites"

# g++
if command -v g++ &>/dev/null; then
    ok "g++ found: $(g++ --version | head -1)"
else
    if [ -f "$SCRIPT_DIR/bridge/chessmind" ] || [ -f "$SCRIPT_DIR/engine/chessmind" ]; then
        warn "g++ not found, but precompiled chessmind binary exists. Skipping compilation check."
    else
        fail "g++ not found. Install with: sudo apt install g++ (Ubuntu) or xcode-select --install (macOS)"
    fi
fi

# python3
if command -v python3 &>/dev/null; then
    ok "Python3 found: $(python3 --version)"
else
    fail "Python3 not found. Install from python.org"
fi

# stockfish (optional but needed for hints)
if command -v stockfish &>/dev/null; then
    ok "Stockfish found: $(stockfish --help 2>/dev/null | head -1 || echo 'stockfish')"
    STOCKFISH_PATH="stockfish"
else
    if [ "$(uname)" = "Linux" ]; then
        info "Stockfish not found, attempting to download precompiled Linux Stockfish..."
        if curl -L -s -o stockfish.tar "https://github.com/official-stockfish/Stockfish/releases/download/sf_16/stockfish-ubuntu-x86-64-modern.tar" || \
           wget -q -O stockfish.tar "https://github.com/official-stockfish/Stockfish/releases/download/sf_16/stockfish-ubuntu-x86-64-modern.tar"; then
            tar -xf stockfish.tar
            if [ -f stockfish/stockfish-ubuntu-x86-64-modern ]; then
                mkdir -p "$SCRIPT_DIR/bridge"
                mv stockfish/stockfish-ubuntu-x86-64-modern "$SCRIPT_DIR/bridge/stockfish_bin"
                chmod +x "$SCRIPT_DIR/bridge/stockfish_bin"
                STOCKFISH_PATH="$SCRIPT_DIR/bridge/stockfish_bin"
                ok "Downloaded and configured Stockfish successfully!"
            else
                warn "Could not find stockfish binary inside extracted archive."
                STOCKFISH_PATH=""
            fi
            rm -rf stockfish.tar stockfish
        else
            warn "Failed to download Stockfish. Hints and evaluation bar will be disabled."
            STOCKFISH_PATH=""
        fi
    else
        warn "Stockfish not found — hints will be disabled. Install: sudo apt install stockfish"
        STOCKFISH_PATH=""
    fi
fi

# pip packages
head "Step 2 — Installing Python packages"
info "Installing fastapi, uvicorn, python-chess..."
pip3 install fastapi uvicorn python-chess websockets --quiet --break-system-packages 2>/dev/null || \
pip3 install fastapi uvicorn python-chess websockets --quiet 2>/dev/null || \
warn "pip install failed — try manually: pip3 install fastapi uvicorn python-chess websockets"

python3 -c "import fastapi, uvicorn, chess" 2>/dev/null && ok "Python packages ready" || warn "Some Python packages missing"

# ─── Step 3: Compile the C++ engine ──────────────────────────────────────────
head "Step 3 — Compiling ChessMind C++ engine"
cd "$SCRIPT_DIR/engine"

if command -v g++ &>/dev/null; then
    info "Compiling (this takes ~5 seconds)..."
    if g++ -std=c++17 -O3 -march=native -o chessmind \
        main.cpp board.cpp movegen.cpp eval.cpp search.cpp 2>&1; then
        ok "Engine compiled successfully"
    else
        warn "O3+native failed, trying safer flags..."
        g++ -std=c++17 -O2 -o chessmind \
            main.cpp board.cpp movegen.cpp eval.cpp search.cpp || fail "Compilation failed"
        ok "Engine compiled (O2 mode)"
    fi
else
    if [ -f "$SCRIPT_DIR/bridge/chessmind" ]; then
        ok "Using existing precompiled binary in bridge/"
    elif [ -f "$SCRIPT_DIR/engine/chessmind" ]; then
        ok "Using existing precompiled binary in engine/"
    else
        fail "g++ not found and no precompiled binary exists to compile the engine."
    fi
fi

# ─── Step 4: Verify engine with perft ────────────────────────────────────────
head "Step 4 — Verifying move generation (perft tests)"
if command -v g++ &>/dev/null; then
    info "Compiling perft tests..."
    g++ -std=c++17 -O3 -o perft perft.cpp board.cpp movegen.cpp eval.cpp search.cpp 2>/dev/null || \
    g++ -std=c++17 -O2 -o perft perft.cpp board.cpp movegen.cpp eval.cpp search.cpp

    info "Running perft suite..."
    PERFT_OUT=$(./perft 2>&1 | strings 2>/dev/null || ./perft 2>&1)
    if echo "$PERFT_OUT" | grep -q "All tests passed"; then
        ok "All 11/11 perft tests passed — move generation correct"
    elif echo "$PERFT_OUT" | grep -q "11/11"; then
        ok "All 11/11 perft tests passed"
    else
        PASSED=$(echo "$PERFT_OUT" | grep -oP '\d+(?=/11)' | tail -1)
        warn "Perft: $PASSED/11 tests passed — engine may have minor bugs"
    fi
else
    warn "g++ not found — skipping perft verification tests."
fi

# ─── Step 5: Copy engine to bridge ───────────────────────────────────────────
head "Step 5 — Setting up bridge"
if [ -f "$SCRIPT_DIR/engine/chessmind" ]; then
    cp "$SCRIPT_DIR/engine/chessmind" "$SCRIPT_DIR/bridge/chessmind"
    ok "Engine binary copied to bridge/"
elif [ -f "$SCRIPT_DIR/bridge/chessmind" ]; then
    ok "Engine binary already present in bridge/"
else
    fail "Engine binary not found."
fi
chmod +x "$SCRIPT_DIR/bridge/chessmind" 2>/dev/null || true

# Quick engine sanity check
ENGINE_TEST=$(echo -e "uci\nisready\nquit" | "$SCRIPT_DIR/bridge/chessmind" 2>/dev/null)
if echo "$ENGINE_TEST" | grep -q "readyok"; then
    ok "Engine UCI handshake works"
else
    warn "Engine UCI test failed — check compilation"
fi

# ─── Step 6: Write .env for bridge ───────────────────────────────────────────
cd "$SCRIPT_DIR/bridge"
cat > .env << ENV
CHESSMIND_BIN=$SCRIPT_DIR/bridge/chessmind
STOCKFISH_PATH=${STOCKFISH_PATH:-stockfish}
ENV
ok "Environment config written"

# ─── Done ─────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}${BOLD}  Setup complete! Run the game:${NC}"
echo -e "${GREEN}${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "  ${BOLD}Option A — One command (recommended):${NC}"
echo -e "  ${CYAN}./run.sh${NC}"
echo ""
echo -e "  ${BOLD}Option B — Manual:${NC}"
echo -e "  Terminal 1: ${CYAN}cd bridge && uvicorn server:app --port 8000${NC}"
echo -e "  Browser:    ${CYAN}open frontend/index.html${NC}"
echo ""
