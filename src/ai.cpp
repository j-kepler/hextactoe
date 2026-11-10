// Computer opponent.
//
// The board is scored through "windows": every run of six consecutive cells
// along one of the three axes. A window holding tiles of only one colour is
// still winnable by that colour and is worth more the fuller it is. A window
// that one more turn (two tiles) can complete is a *threat*: the other side
// must put a tile in it or lose.
//
// For its turn the computer tries pairs of cells, scores each pair with the
// window heuristic plus a tactical check (does it leave an enemy threat open?
// does it create threats the enemy cannot block with two tiles?), then checks
// the most promising pairs one turn deeper so it does not let the opponent
// set up an attack it cannot stop.

#include "ai.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_set>

namespace {

constexpr int kLen = Game::kWinLength;

struct Window {
    Hex start;
    int axis = 0;

    Hex cell(int i) const { return start + kAxes[axis] * i; }
    bool contains(Hex h) const {
        for (int i = 0; i < kLen; ++i) {
            if (cell(i) == h) return true;
        }
        return false;
    }
    bool operator==(const Window& o) const { return start == o.start && axis == o.axis; }
};

struct WindowHash {
    std::size_t operator()(const Window& w) const noexcept {
        return HexHash{}(w.start) ^ (std::size_t(w.axis) * 0x9e3779b97f4a7c15ULL);
    }
};

// Heuristic worth of a window that holds tiles of only one colour, indexed
// by how many tiles it holds. Enemy windows weigh a little more because the
// enemy moves next.
constexpr double kOwnValue[kLen + 1] = {0, 1, 6, 30, 160, 190, 0};
constexpr double kOppValue[kLen + 1] = {0, 1, 7, 40, 200, 230, 0};

constexpr double kWinScore = 1e9;          // opponent cannot stop us next turn
constexpr double kLossScore = -1e9;        // opponent wins on their next turn
constexpr double kForcedLossPenalty = -5e8;  // opponent can force a win in two turns
constexpr double kForcingBonus[3] = {0, 400, 2500};  // by blockers the enemy must spend

constexpr int kFirstTileCandidates = 14;
constexpr int kDeepChecks = 24;

struct Plan {
    std::vector<Hex> cells;
    double score = 0;
};

class Planner {
public:
    Planner(const Game& g, std::uint32_t seed)
        : board_(g.board()),
          me_(g.toMove()),
          them_(opponent(me_)),
          tiles_(g.tilesLeftThisTurn()),
          rng_(seed) {}

    std::vector<Hex> run();

private:
    Player at(Hex h) const {
        const auto it = board_.find(h);
        return it == board_.end() ? Player::None : it->second;
    }

    void count(const Window& w, Player p, int& own, int& other) const {
        own = other = 0;
        for (int i = 0; i < kLen; ++i) {
            const Player c = at(w.cell(i));
            if (c == p) {
                ++own;
            } else if (c != Player::None) {
                ++other;
            }
        }
    }

    std::vector<Hex> emptiesOf(const Window& w) const {
        std::vector<Hex> out;
        for (int i = 0; i < kLen; ++i) {
            if (at(w.cell(i)) == Player::None) out.push_back(w.cell(i));
        }
        return out;
    }

    template <class F>
    static void forEachWindowThrough(Hex h, F&& f) {
        for (int a = 0; a < 3; ++a) {
            for (int o = 0; o < kLen; ++o) f(Window{h - kAxes[a] * o, a});
        }
    }

    static double value(int mine, int theirs) {
        double v = 0;
        if (theirs == 0) v += kOwnValue[mine];
        if (mine == 0) v -= kOppValue[theirs];
        return v;
    }

    void place(const std::vector<Hex>& cells, Player p) {
        for (Hex h : cells) board_[h] = p;
    }
    void remove(const std::vector<Hex>& cells) {
        for (Hex h : cells) board_.erase(h);
    }

    double placeDelta(Hex h) const;
    void collectThreats(Hex h, Player p, std::vector<Window>& out) const;
    std::vector<Window> myThreatsAfter(const std::vector<Hex>& placed) const;
    int blockersNeeded(const std::vector<Window>& threats) const;
    double tactical(const std::vector<Hex>& placed) const;
    bool opponentForcesWin(const std::vector<Window>& myThreats);

    Game::Board board_;
    Player me_;
    Player them_;
    int tiles_;
    std::mt19937 rng_;
    std::vector<Window> live_;        // windows holding at least one tile
    std::vector<Window> oppThreats_;  // enemy windows completable next turn
    std::vector<Window> myThreats_;   // our windows completable next turn
};

// Change in the heuristic if we colour the empty cell `h`.
double Planner::placeDelta(Hex h) const {
    double d = 0;
    forEachWindowThrough(h, [&](const Window& w) {
        int own = 0;
        int other = 0;
        count(w, me_, own, other);
        d += value(own + 1, other) - value(own, other);
    });
    return d;
}

// Adds the threat windows of `p` that pass through `h` (deduplicated).
void Planner::collectThreats(Hex h, Player p, std::vector<Window>& out) const {
    forEachWindowThrough(h, [&](const Window& w) {
        int own = 0;
        int other = 0;
        count(w, p, own, other);
        if (other == 0 && own >= kLen - 2 && std::find(out.begin(), out.end(), w) == out.end()) {
            out.push_back(w);
        }
    });
}

std::vector<Window> Planner::myThreatsAfter(const std::vector<Hex>& placed) const {
    std::vector<Window> threats = myThreats_;
    for (Hex h : placed) collectThreats(h, me_, threats);
    return threats;
}

// Fewest tiles that touch every window: 0, 1, 2, or 3 meaning "more than two".
int Planner::blockersNeeded(const std::vector<Window>& threats) const {
    if (threats.empty()) return 0;
    std::vector<std::vector<Hex>> sets;
    std::vector<Hex> cells;
    for (const Window& w : threats) {
        std::vector<Hex> e = emptiesOf(w);
        if (e.empty()) return 3;  // already complete
        for (Hex h : e) {
            if (std::find(cells.begin(), cells.end(), h) == cells.end()) cells.push_back(h);
        }
        sets.push_back(std::move(e));
    }
    auto has = [](const std::vector<Hex>& s, Hex h) {
        return std::find(s.begin(), s.end(), h) != s.end();
    };
    for (Hex c : cells) {
        if (std::all_of(sets.begin(), sets.end(), [&](const auto& s) { return has(s, c); })) return 1;
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            if (std::all_of(sets.begin(), sets.end(),
                            [&](const auto& s) { return has(s, cells[i]) || has(s, cells[j]); })) {
                return 2;
            }
        }
    }
    return 3;
}

// Score adjustments for threats once `placed` (our tiles) are on the board.
double Planner::tactical(const std::vector<Hex>& placed) const {
    int open = 0;
    for (const Window& w : oppThreats_) {
        const bool blocked = std::any_of(placed.begin(), placed.end(),
                                         [&](Hex h) { return w.contains(h); });
        if (!blocked) ++open;
    }
    if (open > 0) return kLossScore - 1e6 * open;

    const int need = blockersNeeded(myThreatsAfter(placed));
    if (need > 2) return kWinScore;
    return kForcingBonus[need];
}

// With our tiles on the board: can the opponent, while blocking all of our
// threats, create threats that we cannot block with our two tiles?
bool Planner::opponentForcesWin(const std::vector<Window>& myThreats) {
    std::vector<std::vector<Hex>> mustHit;
    std::vector<Hex> cells;
    std::unordered_set<Hex, HexHash> seen;
    auto add = [&](Hex h) {
        if (seen.insert(h).second) cells.push_back(h);
    };
    for (const Window& w : myThreats) {
        mustHit.push_back(emptiesOf(w));
        for (Hex h : mustHit.back()) add(h);
    }
    // Only cells in windows where the enemy already has two tiles can help
    // them reach a threat (four or more) with a single turn.
    for (const Window& w : live_) {
        int own = 0;
        int other = 0;
        count(w, them_, own, other);
        if (other == 0 && own >= 2) {
            for (Hex h : emptiesOf(w)) add(h);
        }
    }

    auto has = [](const std::vector<Hex>& s, Hex h) {
        return std::find(s.begin(), s.end(), h) != s.end();
    };
    std::vector<Window> threats;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            const Hex a = cells[i];
            const Hex b = cells[j];
            const bool blocksUs = std::all_of(mustHit.begin(), mustHit.end(), [&](const auto& s) {
                return has(s, a) || has(s, b);
            });
            if (!blocksUs) continue;  // they would lose to our threat instead
            board_[a] = them_;
            board_[b] = them_;
            threats.clear();
            collectThreats(a, them_, threats);
            collectThreats(b, them_, threats);
            const bool wins = blockersNeeded(threats) > 2;
            board_.erase(a);
            board_.erase(b);
            if (wins) return true;
        }
    }
    return false;
}

std::vector<Hex> Planner::run() {
    if (board_.empty()) {
        std::vector<Hex> out{Hex{0, 0}};
        if (tiles_ == 2) out.push_back(Hex{1, 0});
        return out;
    }

    std::unordered_set<Window, WindowHash> seenWindows;
    for (const auto& entry : board_) {
        forEachWindowThrough(entry.first, [&](const Window& w) {
            if (seenWindows.insert(w).second) live_.push_back(w);
        });
    }

    // 1. Win right now if we can.
    const Window* winning = nullptr;
    int fewest = kLen;
    for (const Window& w : live_) {
        int own = 0;
        int other = 0;
        count(w, me_, own, other);
        if (other == 0 && kLen - own <= tiles_ && kLen - own < fewest) {
            fewest = kLen - own;
            winning = &w;
        }
    }
    if (winning) return emptiesOf(*winning);

    // 2. Threats on both sides.
    for (const Window& w : live_) {
        int own = 0;
        int other = 0;
        count(w, me_, own, other);
        if (own == 0 && other >= kLen - 2) oppThreats_.push_back(w);
        if (other == 0 && own >= kLen - 2) myThreats_.push_back(w);
    }

    // 3. Candidate cells: empty cells near the action.
    std::vector<Hex> cands;
    std::unordered_set<Hex, HexHash> seenCells;
    auto addCand = [&](Hex c) {
        if (at(c) == Player::None && seenCells.insert(c).second) cands.push_back(c);
    };
    for (const auto& entry : board_) {
        for (int dq = -2; dq <= 2; ++dq) {
            for (int dr = -2; dr <= 2; ++dr) {
                const Hex c = entry.first + Hex{dq, dr};
                if (hexDistance(c, entry.first) <= 2) addCand(c);
            }
        }
    }
    std::vector<Hex> mustBlock;
    for (const Window& w : oppThreats_) {
        for (Hex e : emptiesOf(w)) {
            addCand(e);
            if (std::find(mustBlock.begin(), mustBlock.end(), e) == mustBlock.end()) mustBlock.push_back(e);
        }
    }

    std::vector<double> delta(cands.size());
    for (std::size_t i = 0; i < cands.size(); ++i) delta[i] = placeDelta(cands[i]);
    std::uniform_real_distribution<double> jitter(0.0, 0.75);

    // 4. Score every plan for this turn.
    std::vector<Plan> plans;
    if (tiles_ == 1) {
        for (std::size_t i = 0; i < cands.size(); ++i) {
            const std::vector<Hex> cells{cands[i]};
            place(cells, me_);
            plans.push_back({cells, delta[i] + tactical(cells) + jitter(rng_)});
            remove(cells);
        }
    } else {
        std::vector<std::size_t> order(cands.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return delta[x] > delta[y]; });
        std::vector<std::size_t> firsts(order.begin(),
                                        order.begin() + std::min<std::size_t>(order.size(), kFirstTileCandidates));
        for (std::size_t i = 0; i < cands.size(); ++i) {
            if (std::find(mustBlock.begin(), mustBlock.end(), cands[i]) != mustBlock.end() &&
                std::find(firsts.begin(), firsts.end(), i) == firsts.end()) {
                firsts.push_back(i);
            }
        }
        for (std::size_t ia : firsts) {
            const Hex a = cands[ia];
            board_[a] = me_;
            for (std::size_t ib = 0; ib < cands.size(); ++ib) {
                if (ib == ia) continue;
                const Hex b = cands[ib];
                const double db = placeDelta(b);
                board_[b] = me_;
                plans.push_back({{a, b}, delta[ia] + db + tactical({a, b}) + jitter(rng_)});
                board_.erase(b);
            }
            board_.erase(a);
        }
    }
    if (plans.empty()) return {};

    // 5. Look one turn deeper, best plans first. The first plan that is
    // decided, or that does not hand the opponent a forced win, is our move.
    std::sort(plans.begin(), plans.end(), [](const Plan& x, const Plan& y) { return x.score > y.score; });
    const std::size_t checks = std::min<std::size_t>(plans.size(), kDeepChecks);
    for (std::size_t i = 0; i < checks; ++i) {
        Plan& p = plans[i];
        if (p.score >= kWinScore / 2 || p.score <= kLossScore / 2) return p.cells;
        place(p.cells, me_);
        const bool forced = opponentForcesWin(myThreatsAfter(p.cells));
        remove(p.cells);
        if (!forced) return p.cells;
        p.score += kForcedLossPenalty;
    }
    // Every plan we examined loses by force; play the one the heuristic likes best.
    return plans.front().cells;
}

}  // namespace

std::vector<Hex> planTurn(const Game& game, std::uint32_t seed) {
    if (game.isOver()) return {};
    return Planner(game, seed).run();
}
