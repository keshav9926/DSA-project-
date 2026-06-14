#pragma once
/**
 * @file search.h
 * @brief Alpha-Beta Pruned Minimax Search Engine
 * 
 * ALGORITHM: Minimax with Alpha-Beta Pruning
 * ============================================
 * 
 * Minimax is a recursive algorithm for two-player zero-sum games:
 * - Maximizing player (WHITE) tries to maximize score
 * - Minimizing player (BLACK) tries to minimize score
 * - Returns minimax value: game outcome with perfect play
 * 
 * Alpha-Beta Pruning:
 * - alpha: Best value maximizer can guarantee
 * - beta: Best value minimizer can guarantee
 * - When alpha >= beta: branch is irrelevant, can be skipped
 * 
 * COMPLEXITY ANALYSIS:
 * ====================
 * Time Complexity:
 *   Pure Minimax:        O(b^d)           where b=branching factor, d=depth
 *   With alpha-beta:     O(b^(3d/4))      best move ordering
 *   Random move order:   O(b^d)           degrades to full tree
 *   Best case (perfect): O(b^(d/2))       ideal pruning
 * 
 * Space Complexity:
 *   Recursion stack:     O(d)             max depth at any time
 *   Move generation:     O(b)             moves per position
 *   Transposition table: O(2^16) ~1MB     configurable cache
 * 
 * Real-world improvements (chess, depth=10):
 *   Without optimization: ~35^10 ≈ 2.76 × 10^15 positions (impossible)
 *   With alpha-beta:      ~100 million positions (1-2 seconds)
 *   Speedup:              10^7 times faster!
 * 
 * KEY TECHNIQUES:
 * ===============
 * 1. Alpha-Beta Pruning: Skip unreachable branches
 * 2. Move Ordering: Evaluate best moves first (TT, captures, killer moves)
 * 3. Transposition Table: Cache evaluated positions (Zobrist hashing)
 * 4. Null Move Pruning: Don't move if opponent would pass (reduction of moves)
 * 5. Quiescence Search: Search captures past depth limit (avoid horizon effect)
 * 6. Killer Heuristic: Same move kills differently-ordered siblings
 * 7. History Heuristic: Prioritize moves that caused cutoffs before
 */

#include "board.h"
#include "movegen.h"
#include "eval.h"
#include <atomic>
#include <chrono>
#include <string>

// ─── TRANSPOSITION TABLE: MEMOIZATION FOR GAME POSITIONS ────────────────────
/**
 * @brief Transposition Table Entry
 * 
 * Purpose: Cache evaluation of previously-seen positions
 * 
 * Problem: Same position can occur from different move sequences
 *   Example: Move 1: e4 c5, Move 2: Nf3 d6
 *            vs Move 1: e4 c5, Move 2: Nf3 d6  (same position)
 * 
 * Solution: Store (position hash → evaluation) pairs
 * 
 * Flag Meanings:
 *   EXACT: score is exact minimax value at this depth
 *   LOWER: evaluated positions only, score >= actual minimax value
 *   UPPER: pruning may have occurred, score <= actual minimax value
 * 
 * Data Structure: Hash Table
 *   - Lookup: O(1) average case
 *   - Hit rate in chess: ~45-55%
 *   - Memory efficiency: ~50% of tree nodes, 2x speedup
 * 
 * Zobrist Hashing:
 *   - Hash = XOR of all piece positions + game state
 *   - Incremental: Can update in O(1) when pieces move
 *   - Collision probability: < 10^-18 for 64-bit hashes
 */
enum TTFlag { 
    TT_EXACT,   ///< Exact minimax value
    TT_LOWER,   ///< Lower bound (alpha cutoff)
    TT_UPPER    ///< Upper bound (beta cutoff)
};

struct TTEntry {
    uint64_t hash;      ///< Zobrist hash of position (for verification)
    int      depth;     ///< Search depth when evaluated
    Score    score;     ///< Minimax evaluation
    Move     bestMove;  ///< Best move found at this position
    TTFlag   flag;      ///< Type of bound
};

/**
 * @class TranspositionTable
 * @brief Hash-based cache for position evaluations
 * 
 * Design: Open Addressing Hash Table (collision = overwrite with better entry)
 * Size: Default TT_SIZE = 65536 entries (~1 MB memory)
 * Collision Resolution: Replace if new depth >= stored depth
 * 
 * Operations:
 *   probe() - O(1) lookup
 *   store() - O(1) insertion
 *   clear() - O(n) initialization
 */
class TranspositionTable {
public:
    /// Array of TT entries
    std::vector<TTEntry> table;
    
    /// Initialize with given size (power of 2 for efficient hashing)
    TranspositionTable(int size=TT_SIZE) : table(size) {}
    
    /// Clear all entries (for new game or position)
    void clear(){
        for(auto& e:table) e.hash=0;
    }
    
    /// Lookup position in table - O(1) average case
    /// @param hash Zobrist hash of position
    /// @return Pointer to entry if found, nullptr otherwise
    TTEntry* probe(uint64_t hash){
        auto& e=table[hash%table.size()];
        // Verify hash match (avoid hash collisions)
        return (e.hash==hash)?&e:nullptr;
    }
    
    /// Store evaluation in table - O(1) average case
    /// @param hash Zobrist hash
    /// @param depth Depth searched
    /// @param score Evaluation score
    /// @param best Best move found
    /// @param flag Type of bound
    void store(uint64_t hash, int depth, Score score, Move best, TTFlag flag){
        auto& e=table[hash%table.size()];
        // Only replace if new entry is from deeper search (more reliable)
        if(e.hash==0 || depth >= e.depth){
            e={hash, depth, score, best, flag};
        }
    }
};

// ─── SEARCH STATISTICS: FOR UI AND ANALYSIS ────────────────────────────────
/**
 * @struct SearchInfo
 * @brief Statistics from search (sent to UI and analysis tools)
 * 
 * Used to display:
 * - Current search depth
 * - Evaluation score
 * - Nodes per second (NPS) - cache performance
 * - Principal variation (PV) - best move sequence found
 */
struct SearchInfo {
    long long nodes       = 0;    ///< Total nodes evaluated
    long long nodesPerSec = 0;    ///< Search speed
    int       depth       = 0;    ///< Current main search depth
    int       seldepth    = 0;    ///< Selective depth (quiescence extension)
    Score     score       = 0;    ///< Best evaluation found
    Move      bestMove    = 0;    ///< Best move to play
    std::string pvLine;           ///< Principal Variation (best line)
    long long timeMs      = 0;    ///< Elapsed time
    long long pruned      = 0;    ///< Nodes eliminated by pruning
};

// ─── MAIN SEARCH ENGINE ─────────────────────────────────────────────────────
/**
 * @class Search
 * @brief Minimax with Alpha-Beta Pruning Implementation
 * 
 * Core Algorithm Flow:
 * ====================
 * 1. Call startSearch() with board, depth limit, and time limit
 * 2. Iterative deepening (1-ply, 2-ply, ..., target depth)
 * 3. For each depth iteration, call alphaBeta() recursively
 * 4. Returns best move found when time/depth exhausted
 * 
 * Optimization Techniques:
 * =======================
 * a) Transposition Table: Memoization via Zobrist hashing
 * b) Move Ordering: TT move → Captures (MVV-LVA) → Killers → History
 * c) Alpha-Beta Pruning: Skip impossible branches
 * d) Null Move Pruning: Skip if opponent can't improve with free turn
 * e) Killer Heuristic: Save moves that caused cutoffs
 * f) History Heuristic: Count moves causing cutoffs at each position
 * g) Quiescence Search: Extend search for forcing moves (captures)
 * h) Mate Distance Pruning: Shorten alpha/beta for found mates
 * 
 * Memory Layout:
 * ==============
 * killerMoves[ply][2]: Best cutoff moves at each ply
 * historyTable[side][piece][square]: Cutoff statistics
 * 
 * Search Depths in Chess:
 * Beginner (2):     Quick moves, obvious tactics
 * Intermediate (5): Balanced gameplay
 * Hard (8):         Strong moves, good tactics
 * Expert (12):      Very strong, near-expert level
 */
class Search {
public:
    // ─ Cache structures for optimization ────────────────────────────────
    
    /// Transposition table for memoization
    TranspositionTable tt;
    
    /// Current search statistics
    SearchInfo info;
    
    /// Signal to stop search (time limit or user abort)
    std::atomic<bool> stop{false};
    
    /// Killer moves: moves that caused cutoffs at each ply
    /// killermoves[ply][0] = primary killer, [ply][1] = secondary
    /// Used for move ordering without position evaluation
    /// Memory: 2 × MAX_PLY × 32 bits = ~1 KB
    Move killerMoves[MAX_PLY][2] = {};
    
    /// History heuristic: count of cutoffs per piece-square
    /// historyTable[WHITE/BLACK][piece_type][destination_square]
    /// Used to prioritize moves that have historically caused cutoffs
    /// Memory: 2 × 6 × 64 × 32 bits = 3 KB
    int historyTable[2][6][64] = {};

    // ─ Public search interface ──────────────────────────────────────────
    
    /**
     * Start the search engine
     * 
     * Algorithm: Iterative Deepening with Time Management
     * - Try depths 1, 2, 3, ..., maxDepth
     * - Stop if time limit reached or depth completed
     * - Returns best move found at latest completed depth
     * 
     * @param board Current board position
     * @param maxDepth Maximum search depth (typically 2-12)
     * @param timeLimitMs Time limit in milliseconds
     * @return Best move found (in 16-bit format)
     * 
     * Time Complexity: O(nodes evaluated × evaluation)
     *   typical: 10M-100M nodes depending on depth
     * Space Complexity: O(maxDepth)
     *   (recursion stack + TT memory)
     */
    Move startSearch(Board& board, int maxDepth, int timeLimitMs);

    /// Get current search info (nodes, score, PV, etc.)
    SearchInfo getInfo() const { return info; }

private:
    // ─ Time management ─────────────────────────────────────────────────
    
    /// Timestamp when search started
    std::chrono::steady_clock::time_point startTime;
    
    /// Current time limit in milliseconds
    int timeLimitMs = 2000;

    // ─ Core search algorithms ──────────────────────────────────────────
    
    /**
     * Alpha-Beta Pruning Minimax Algorithm
     * 
     * Algorithm Logic:
     * ================
     * pseudocode:
     *   alphaBeta(position, depth, ply, alpha, beta):
     *     if depth == 0: return evaluate(position)
     *     
     *     for each move:
     *       make_move()
     *       value = -alphaBeta(depth-1, ply+1, -beta, -alpha)
     *       undo_move()
     *     
     *       alpha = max(alpha, value)
     *       if alpha >= beta: break  // Beta cutoff - prune!
     *     
     *     return alpha
     * 
     * Optimizations Implemented:
     * - Transposition table lookup/store
     * - Move ordering (TT → captures → killers)
     * - Early termination checks
     * - Null move pruning (with reduction)
     * - Mate distance pruning
     * - Quiescence search extension
     * 
     * @param b Board position
     * @param depth Remaining search depth
     * @param ply Distance from root (for killer/history indexing)
     * @param alpha Best value maximizer can guarantee
     * @param beta Best value minimizer can guarantee
     * @param nullOk Allow null move pruning?
     * @return Evaluation score at this position
     * 
     * Time Complexity: O(b^(3d/4)) typical with good move ordering
     */
    Score alphaBeta(Board& b, int depth, int ply, Score alpha, Score beta, 
                   bool nullOk);

    /**
     * Quiescence Search - Extend search for forcing moves
     * 
     * Problem: Horizon Effect
     * ======================
     * Without quiescence:
     *   eval(...my queen attacked by pawn...)
     *   → Looks bad, engine avoids it
     *   → But pawn can't actually capture (protected)
     *   → Wrong evaluation!
     * 
     * Solution: Quiet positions
     * ==========================
     * When reaching depth limit in main search, continue searching
     * but only FORCING moves: captures and checks
     * This ensures we evaluate positions after immediate tactics
     * 
     * Implementation:
     * - Generate only captures (not all moves)
     * - Apply same alpha-beta logic
     * - Prune obviously losing captures (capture + 200 < alpha)
     * - Typical extension: 3-6 extra plies
     * 
     * @param b Board position
     * @param alpha Alpha bound
     * @param beta Beta bound
     * @param ply Current search depth
     * @return Quiescence value (forcing moves only)
     * 
     * Time Complexity: O(captures^plyExtension) - much smaller than main search
     */
    Score quiescence(Board& b, Score alpha, Score beta, int ply);

    // ─ Move ordering - critical for alpha-beta effectiveness ─────────────
    
    /**
     * Sort moves by expected quality for pruning effectiveness
     * 
     * Heuristics (in priority order):
     * 1. Transposition table move (TT) - already proven best
     * 2. Captures - sort by MVV-LVA (Most Valuable Victim - Least Valuable Attacker)
     * 3. Killer moves - caused cutoffs elsewhere
     * 4. History moves - historically caused cutoffs
     * 
     * Impact on search:
     * - Good ordering: O(b^(d/2)) with alpha-beta
     * - Random order: O(b^d) - no pruning benefit
     * - Difference: 10,000x on deptgh 10!
     * 
     * @param b Board position
     * @param moves Vector of moves to sort (sorted in-place)
     * @param ply Current search ply
     * @param ttMove Transposition table move (best if found)
     */
    void orderMoves(Board& b, std::vector<Move>& moves, int ply, Move ttMove);

    /**
     * MVV-LVA (Most Valuable Victim - Least Valuable Attacker)
     * 
     * Simple capture sort heuristic:
     * Score = Victim_Value × 10 - Attacker_Value
     * 
     * Captures to try first: Queen × Pawn, Queen × Knight, etc.
     * Captures to try last: Pawn × Queen, Knight × Queen, etc.
     * 
     * Time: O(1) per move
     */
    int mvvLva(int victim, int attacker) const;

    /**
     * Assign priority score to a move for ordering
     * 
     * Scoring: Higher = better (try first)
     * - TT move:           2,000,000 (almost certain best)
     * - Capture:           1,000,000 + MVV-LVA bonus
     * - Killer 1:          900,000
     * - Killer 2:          890,000
     * - History:           0-50,000 (based on cutoff frequency)
     * 
     * Time: O(1)
     */
    int scoreMove(Board& b, Move m, int ply, Move ttMove) const;

    /**
     * Check if time limit exceeded
     * 
     * Optimization: Only check every 2048 nodes (avoid overhead)
     * Typical check time: 1-2 microseconds (cheap enough)
     * 
     * Time: O(1)
     * 
     * @return true if time limit reached
     */
    bool timeUp() const;

    /**
     * Reconstruct principal variation (best line found)
     * 
     * Algorithm: Follow TT best moves from position
     * Stops when depth reached or TT entry not found
     * 
     * Used for UI display of "engine thinking"
     * 
     * @param b Current board
     * @param depth Maximum depth to follow
     * @return String of moves in algebraic notation
     * 
     * Time: O(depth) × lookup time
     */
    std::string buildPV(Board& b, int depth);
};
