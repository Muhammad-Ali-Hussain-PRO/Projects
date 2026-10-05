#ifndef TEMPO_CORE_HPP
#define TEMPO_CORE_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// A deterministic, headless RTS. Policies receive Observation, never Engine.
namespace tempo {

constexpr int width = 32;
constexpr int height = 18;
constexpr int max_population = 24;
constexpr int max_orders = 4;
enum class Side { Blue = 0, Red = 1 };
enum class Kind { Base, Worker, Soldier, Scout, Tower };
enum class ActionType { Move, Gather, AttackMove, Build, Stop };
enum class CommandType { Order, Train, Construct };

inline int index(Side side) { return static_cast<int>(side); }
inline Side other(Side side) { return side == Side::Blue ? Side::Red : Side::Blue; }
inline const char* name(Side side) { return side == Side::Blue ? "blue" : "red"; }
inline const char* name(Kind kind) {
    switch (kind) {
        case Kind::Base: return "base";
        case Kind::Worker: return "worker";
        case Kind::Soldier: return "soldier";
        case Kind::Scout: return "scout";
        case Kind::Tower: return "tower";
    }
    return "unknown";
}
inline const char* name(ActionType action) {
    switch (action) {
        case ActionType::Move: return "move";
        case ActionType::Gather: return "gather";
        case ActionType::AttackMove: return "attack-move";
        case ActionType::Build: return "build";
        case ActionType::Stop: return "idle";
    }
    return "idle";
}
struct Point {
    int x = 0, y = 0;
    bool operator==(Point p) const { return x == p.x && y == p.y; }
    bool operator!=(Point p) const { return !(*this == p); }
};
inline int distance(Point a, Point b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
inline int tile(Point p) { return p.y * width + p.x; }
inline bool inside(Point p) { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
inline bool mobile(Kind kind) { return kind == Kind::Worker || kind == Kind::Soldier || kind == Kind::Scout; }
inline int maxHp(Kind kind) {
    switch (kind) {
        case Kind::Base: return 240;
        case Kind::Worker: return 35;
        case Kind::Soldier: return 60;
        case Kind::Scout: return 30;
        case Kind::Tower: return 100;
    }
    return 0;
}
inline int sight(Kind kind) { return kind == Kind::Scout ? 8 : kind == Kind::Base || kind == Kind::Tower ? 6 : kind == Kind::Worker ? 4 : 5; }
inline int attackRange(Kind kind) { return kind == Kind::Tower ? 5 : kind == Kind::Base ? 4 : 1; }
inline int damage(Kind kind) { return kind == Kind::Tower ? 10 : kind == Kind::Base || kind == Kind::Soldier ? 8 : 2; }
inline int cost(Kind kind) { return kind == Kind::Worker ? 40 : kind == Kind::Scout ? 35 : kind == Kind::Soldier ? 60 : kind == Kind::Tower ? 90 : 0; }
inline int buildTime(Kind kind) { return kind == Kind::Worker ? 8 : kind == Kind::Scout ? 6 : kind == Kind::Soldier ? 12 : kind == Kind::Tower ? 18 : 0; }

struct Action {
    ActionType type = ActionType::Stop;
    Point target;
    int targetId = -1; // Resource node for Gather, own task for Build.
};
struct Unit {
    int id = 0;
    Side side = Side::Blue;
    Kind kind = Kind::Worker;
    Point position;
    int hp = 35;
    int cargo = 0;
    std::deque<Action> orders;
};
struct BuildTask {
    int id = 0;
    Side side = Side::Blue;
    Kind kind = Kind::Soldier;
    int builderId = 0;
    Point position;
    int remaining = 0;
    int total = 0;
    int reservedCost = 0;
};
struct ResourceNode { int id = 0; Point position; int remaining = 0; };
struct UnitView {
    int id = 0;
    Kind kind = Kind::Worker;
    Point position;
    int hp = 0;
    int cargo = 0;
    bool busy = false;
    int queuedOrders = 0;
    ActionType action = ActionType::Stop;
};
struct Sighting { int id = 0; Kind kind = Kind::Worker; Point position; int hp = 0; int lastSeen = 0; };
struct ResourceView { int id = 0; Point position; int remaining = 0; int lastSeen = 0; };
struct Observation {
    int tick = 0;
    Side side = Side::Blue;
    int resources = 0;
    int score = 0;
    Point enemyStart;
    std::vector<UnitView> own;
    std::vector<UnitView> visibleEnemy;
    std::vector<BuildTask> builds;
    std::vector<Sighting> intel;
    std::vector<ResourceView> mines;
    std::vector<int> visible;
    std::vector<int> explored;
    std::vector<int> blocked;
};
struct Command {
    CommandType type = CommandType::Order;
    int unitId = 0;
    Action action;
    Kind kind = Kind::Worker;
    Point position;
    bool append = false;
    static Command order(int id, ActionType action, Point target, int targetId = -1, bool append = false) {
        Command c; c.unitId = id; c.action = {action, target, targetId}; c.append = append; return c;
    }
    static Command train(int base, Kind kind) { Command c; c.type = CommandType::Train; c.unitId = base; c.kind = kind; return c; }
    static Command construct(int worker, Point position) { Command c; c.type = CommandType::Construct; c.unitId = worker; c.kind = Kind::Tower; c.position = position; return c; }
};

// Work units are explicit and deterministic, unlike timing the host machine.
struct DecisionBudget {
    int limit = 240;
    int used = 0;
    bool exhausted = false;
    explicit DecisionBudget(int amount = 240) : limit(std::max(0, amount)) {}
    bool spend(int amount) {
        if (amount < 0) throw std::invalid_argument("negative work charge");
        if (used + amount > limit) { exhausted = true; return false; }
        used += amount; return true;
    }
};
struct Decision {
    std::vector<Command> commands;
    int budget = 240;
    int used = 0;
    bool missed = false;
};
struct TickResult {
    int tick = 0;
    std::array<int, 2> accepted{{0, 0}};
    std::array<int, 2> rejected{{0, 0}};
    std::array<int, 2> destroyed{{0, 0}};
    std::string text;
};
struct UnitSetup { Side side; Kind kind; Point position; int hp = -1; };
struct Scenario {
    std::vector<UnitSetup> units;
    std::vector<ResourceNode> mines;
    std::array<int, 2> resources{{150, 150}};
};

class Engine {
    int tick_ = 0, nextUnitId_ = 1, nextTaskId_ = 1;
    std::uint32_t seed_;
    std::vector<Unit> units_;
    std::vector<BuildTask> builds_;
    std::vector<ResourceNode> mines_;
    std::array<int, 2> resources_{{150, 150}};
    std::array<int, 2> deposited_{{0, 0}}, kills_{{0, 0}};
    std::array<std::array<bool, width * height>, 2> visible_{};
    std::array<std::array<bool, width * height>, 2> explored_{};
    std::array<bool, width * height> blocked_{};
    std::array<std::map<int, Sighting>, 2> intel_;
    std::array<std::map<int, ResourceView>, 2> mineMemory_;
    int invariantChecks_ = 0;

    Unit* unit(int id) {
        auto it = std::find_if(units_.begin(), units_.end(), [id](const Unit& u) { return u.id == id; });
        return it == units_.end() ? nullptr : &*it;
    }
    const Unit* unit(int id) const {
        auto it = std::find_if(units_.begin(), units_.end(), [id](const Unit& u) { return u.id == id; });
        return it == units_.end() ? nullptr : &*it;
    }
    const Unit* base(Side side) const {
        auto it = std::find_if(units_.begin(), units_.end(), [side](const Unit& u) { return u.side == side && u.kind == Kind::Base; });
        return it == units_.end() ? nullptr : &*it;
    }
    void addUnit(Side side, Kind kind, Point p, int hp = -1) {
        units_.push_back({nextUnitId_++, side, kind, p, hp < 0 ? maxHp(kind) : hp, 0, {}});
    }
    bool passable(Point p) const { return inside(p) && !blocked_[tile(p)]; }
    int population(Side side) const {
        int n = 0;
        for (const auto& u : units_) if (u.side == side && mobile(u.kind)) ++n;
        for (const auto& b : builds_) if (b.side == side && mobile(b.kind)) ++n;
        return n;
    }
    void terrain() {
        // River banks leave a central bridge and outside paths. Terrain is public.
        for (int y = 1; y <= 6; ++y) blocked_[tile({15, y})] = blocked_[tile({16, y})] = true;
        for (int y = 12; y <= 16; ++y) blocked_[tile({15, y})] = blocked_[tile({16, y})] = true;
    }
    Point nextStep(Point start, Point goal, int stopDistance = 0) const {
        if (distance(start, goal) <= stopDistance) return start;
        std::array<int, width * height> parent; parent.fill(-1);
        std::queue<Point> q; q.push(start); parent[tile(start)] = tile(start);
        Point found = start; bool reached = false;
        // Fixed neighbor order gives reproducible shortest paths.
        const std::array<Point, 4> offsets{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
        while (!q.empty()) {
            const Point p = q.front(); q.pop();
            if (distance(p, goal) <= stopDistance) { found = p; reached = true; break; }
            for (Point d : offsets) {
                Point n{p.x + d.x, p.y + d.y};
                if (passable(n) && parent[tile(n)] < 0) { parent[tile(n)] = tile(p); q.push(n); }
            }
        }
        if (!reached) return start;
        int at = tile(found);
        while (parent[at] != tile(start) && parent[at] != at) at = parent[at];
        return {at % width, at / width};
    }
    void move(Unit& u, Point goal, int stopDistance = 0) {
        const int steps = u.kind == Kind::Scout ? 2 : 1;
        for (int step = 0; step < steps; ++step) u.position = nextStep(u.position, goal, stopDistance);
    }
    void updateVision() {
        for (auto& v : visible_) v.fill(false);
        for (const Unit& u : units_) {
            auto& v = visible_[index(u.side)];
            for (int y = std::max(0, u.position.y - sight(u.kind)); y <= std::min(height - 1, u.position.y + sight(u.kind)); ++y)
                for (int x = std::max(0, u.position.x - sight(u.kind)); x <= std::min(width - 1, u.position.x + sight(u.kind)); ++x)
                    if (distance(u.position, {x, y}) <= sight(u.kind)) v[tile({x, y})] = true;
        }
        for (int s = 0; s < 2; ++s) {
            for (int t = 0; t < width * height; ++t) explored_[s][t] = explored_[s][t] || visible_[s][t];
            for (const Unit& u : units_) if (index(u.side) != s && visible_[s][tile(u.position)])
                intel_[s][u.id] = {u.id, u.kind, u.position, u.hp, tick_};
            // A clear, visible old location invalidates stale memory. Invisible
            // deaths and movement do not erase or update enemy intelligence.
            for (auto it = intel_[s].begin(); it != intel_[s].end();) {
                const Unit* current = unit(it->first);
                const bool clearOldLocation = visible_[s][tile(it->second.position)] &&
                    (!current || current->position != it->second.position) &&
                    (!current || !visible_[s][tile(current->position)]);
                if (clearOldLocation || tick_ - it->second.lastSeen > 60) it = intel_[s].erase(it); else ++it;
            }
            for (const auto& mine : mines_) if (visible_[s][tile(mine.position)])
                mineMemory_[s][mine.id] = {mine.id, mine.position, mine.remaining, tick_};
        }
    }
    bool accept(Side side, const Command& c) {
        Unit* u = unit(c.unitId);
        if (!u || u->side != side) return false;
        if (c.type == CommandType::Order) {
            if (!mobile(u->kind) || c.action.type == ActionType::Build) return false;
            if (c.action.type != ActionType::Stop && !passable(c.action.target)) return false;
            if (c.action.type == ActionType::Gather) {
                if (u->kind != Kind::Worker) return false;
                const auto known = mineMemory_[index(side)].find(c.action.targetId);
                if (known == mineMemory_[index(side)].end() || known->second.position != c.action.target) return false;
            }
            // A construction worker cannot be commandeered without cancelling
            // its paid reservation. Reject replacement and keep it building.
            if (!u->orders.empty() && u->orders.front().type == ActionType::Build) return false;
            if (c.action.type == ActionType::Stop) { u->orders.clear(); return true; }
            if (c.append && static_cast<int>(u->orders.size()) >= max_orders) return false;
            if (!c.append) u->orders.clear();
            u->orders.push_back(c.action); return true;
        }
        if (resources_[index(side)] < cost(c.kind)) return false;
        if (c.type == CommandType::Train) {
            if (u->kind != Kind::Base || !mobile(c.kind) || population(side) >= max_population) return false;
            const int slots = static_cast<int>(std::count_if(builds_.begin(), builds_.end(), [u](const BuildTask& b) { return b.builderId == u->id; }));
            if (slots >= 2) return false;
            resources_[index(side)] -= cost(c.kind);
            builds_.push_back({nextTaskId_++, side, c.kind, u->id, u->position, buildTime(c.kind), buildTime(c.kind), cost(c.kind)});
            return true;
        }
        if (c.type == CommandType::Construct) {
            if (u->kind != Kind::Worker || c.kind != Kind::Tower || !passable(c.position)) return false;
            if (!visible_[index(side)][tile(c.position)]) return false;
            if (!u->orders.empty() && u->orders.front().type == ActionType::Build) return false;
            if (std::any_of(units_.begin(), units_.end(), [&c](const Unit& v) { return !mobile(v.kind) && v.position == c.position; })) return false;
            if (std::any_of(builds_.begin(), builds_.end(), [&c](const BuildTask& b) { return b.kind == Kind::Tower && b.position == c.position; })) return false;
            resources_[index(side)] -= cost(c.kind);
            const int id = nextTaskId_++;
            builds_.push_back({id, side, Kind::Tower, u->id, c.position, buildTime(Kind::Tower), buildTime(Kind::Tower), cost(Kind::Tower)});
            u->orders.clear(); u->orders.push_back({ActionType::Build, c.position, id}); return true;
        }
        return false;
    }
    void runOrders() {
        const std::vector<Unit> movementSnapshot = units_;
        for (Unit& u : units_) {
            if (u.orders.empty()) continue;
            const Action a = u.orders.front();
            if (a.type == ActionType::Gather) {
                auto it = std::find_if(mines_.begin(), mines_.end(), [a](const ResourceNode& m) { return m.id == a.targetId; });
                const Unit* home = base(u.side);
                if (!home) { u.orders.pop_front(); continue; }
                if (u.cargo >= 12 || it == mines_.end() || it->remaining == 0) {
                    if (distance(u.position, home->position) <= 1) {
                        resources_[index(u.side)] += u.cargo; deposited_[index(u.side)] += u.cargo; u.cargo = 0;
                        if (it == mines_.end() || it->remaining == 0) u.orders.pop_front();
                    } else move(u, home->position, 1);
                } else if (distance(u.position, it->position) <= 1) {
                    const int mined = std::min({4, 12 - u.cargo, it->remaining}); u.cargo += mined; it->remaining -= mined;
                } else move(u, it->position, 1);
            } else if (a.type == ActionType::Build) {
                move(u, a.target, 1);
            } else if (a.type == ActionType::Move || a.type == ActionType::AttackMove) {
                bool engaged = false;
                if (a.type == ActionType::AttackMove)
                    for (const Unit& enemy : movementSnapshot) if (enemy.side != u.side && distance(u.position, enemy.position) <= attackRange(u.kind)) { engaged = true; break; }
                if (!engaged) move(u, a.target);
                if (u.position == a.target) u.orders.pop_front();
            } else u.orders.pop_front();
        }
    }
    int advanceBuilds() {
        std::vector<BuildTask> finished;
        for (auto& b : builds_) {
            const Unit* builder = unit(b.builderId);
            if (builder && (builder->kind == Kind::Base || distance(builder->position, b.position) <= 1)) --b.remaining;
            if (b.remaining == 0) finished.push_back(b);
        }
        for (const auto& b : finished) {
            // New units spawn on a traversable neighboring tile. Stacking is
            // allowed: this is a strategic simulator, not a collision engine.
            Point spawn = b.position;
            if (b.kind != Kind::Tower) {
                const Point candidate{spawn.x + (b.side == Side::Blue ? 1 : -1), spawn.y};
                if (passable(candidate)) spawn = candidate;
            }
            addUnit(b.side, b.kind, spawn);
            Unit* builder = unit(b.builderId);
            if (builder && !builder->orders.empty() && builder->orders.front().type == ActionType::Build && builder->orders.front().targetId == b.id)
                builder->orders.pop_front();
        }
        builds_.erase(std::remove_if(builds_.begin(), builds_.end(), [](const BuildTask& b) { return b.remaining == 0; }), builds_.end());
        return static_cast<int>(finished.size());
    }
    std::array<int, 2> combat() {
        std::map<int, int> incoming;
        std::map<int, Side> attackers;
        for (const Unit& u : units_) {
            const Unit* target = nullptr;
            for (const Unit& enemy : units_) {
                if (enemy.side == u.side || distance(u.position, enemy.position) > attackRange(u.kind) || !visible_[index(u.side)][tile(enemy.position)]) continue;
                // Prioritize attackers, then lowest HP, then stable ID.
                const auto rank = [](const Unit& e) { return std::make_pair(e.kind == Kind::Soldier || e.kind == Kind::Tower ? 0 : e.kind == Kind::Base ? 2 : 1, std::make_pair(e.hp, e.id)); };
                if (!target || rank(enemy) < rank(*target)) target = &enemy;
            }
            if (target) { incoming[target->id] += damage(u.kind); attackers[target->id] = u.side; }
        }
        std::array<int, 2> destroyed{{0, 0}};
        for (Unit& u : units_) {
            u.hp -= incoming[u.id];
            if (u.hp <= 0) { ++destroyed[index(u.side)]; ++kills_[index(attackers[u.id])]; }
        }
        units_.erase(std::remove_if(units_.begin(), units_.end(), [](const Unit& u) { return u.hp <= 0; }), units_.end());
        // Death cancels unfinished reservations. Refund a fraction reflecting
        // unbuilt work; fully produced units are unaffected.
        builds_.erase(std::remove_if(builds_.begin(), builds_.end(), [this](const BuildTask& b) {
            if (unit(b.builderId)) return false;
            resources_[index(b.side)] += b.reservedCost * b.remaining / (2 * b.total); return true;
        }), builds_.end());
        return destroyed;
    }
public:
    explicit Engine(std::uint32_t seed = 1) : seed_(seed) {
        terrain(); std::mt19937 rng(seed); const int offset = static_cast<int>(rng() % 3) - 1;
        for (Side side : {Side::Blue, Side::Red}) {
            const int x = side == Side::Blue ? 3 : 28;
            addUnit(side, Kind::Base, {x, 9});
            for (int y = 8; y <= 10; ++y) addUnit(side, Kind::Worker, {x + (side == Side::Blue ? 1 : -1), y});
            addUnit(side, Kind::Soldier, {x + (side == Side::Blue ? 2 : -2), 9});
        }
        mines_ = {{1, {5, 5 + offset}, 900}, {2, {26, 5 + offset}, 900}, {3, {12, 13}, 1200}, {4, {19, 13}, 1200}};
        updateVision(); checkInvariants();
    }
    Engine(std::uint32_t seed, const Scenario& scenario) : seed_(seed), resources_(scenario.resources) {
        terrain(); mines_ = scenario.mines;
        for (const auto& u : scenario.units) addUnit(u.side, u.kind, u.position, u.hp);
        updateVision(); checkInvariants();
    }
    int tick() const { return tick_; }
    std::uint32_t seed() const { return seed_; }
    int resources(Side side) const { return resources_[index(side)]; }
    int score(Side side) const {
        int assets = resources(side) + deposited_[index(side)] / 2 + kills_[index(side)] * 30;
        for (const Unit& u : units_) if (u.side == side) assets += cost(u.kind);
        for (const auto& b : builds_) if (b.side == side) assets += b.reservedCost;
        return assets;
    }
    int invariantChecks() const { return invariantChecks_; }
    bool terminal() const { return !base(Side::Blue) || !base(Side::Red); }
    std::string winner() const {
        if (!base(Side::Blue) && !base(Side::Red)) return "draw";
        if (!base(Side::Red)) return "blue";
        if (!base(Side::Blue)) return "red";
        return "ongoing";
    }
    Observation observe(Side side) const {
        Observation view; view.tick = tick_; view.side = side; view.resources = resources(side); view.score = score(side);
        view.enemyStart = side == Side::Blue ? Point{28, 9} : Point{3, 9};
        for (const Unit& u : units_) {
            const bool own = u.side == side;
            if (own || visible_[index(side)][tile(u.position)]) {
                UnitView v{u.id, u.kind, u.position, u.hp, own ? u.cargo : 0, own && !u.orders.empty(), own ? static_cast<int>(u.orders.size()) : 0,
                    own && !u.orders.empty() ? u.orders.front().type : ActionType::Stop};
                (own ? view.own : view.visibleEnemy).push_back(v);
            }
        }
        for (const auto& b : builds_) if (b.side == side) view.builds.push_back(b);
        for (const auto& item : intel_[index(side)]) view.intel.push_back(item.second);
        for (const auto& item : mineMemory_[index(side)]) view.mines.push_back(item.second);
        for (int t = 0; t < width * height; ++t) {
            if (visible_[index(side)][t]) view.visible.push_back(t);
            if (explored_[index(side)][t]) view.explored.push_back(t);
            if (blocked_[t]) view.blocked.push_back(t);
        }
        return view;
    }
    TickResult step(const Decision& blue, const Decision& red) {
        TickResult result; result.tick = tick_;
        if (terminal()) { result.text = "Match is finished."; return result; }
        const std::array<const Decision*, 2> decisions{{&blue, &red}};
        for (int s = 0; s < 2; ++s) {
            for (const auto& command : decisions[s]->commands) {
                if (accept(static_cast<Side>(s), command)) ++result.accepted[s]; else ++result.rejected[s];
            }
        }
        runOrders(); const int completed = advanceBuilds(); updateVision(); result.destroyed = combat();
        ++tick_; updateVision(); checkInvariants(); result.tick = tick_;
        result.text = std::to_string(result.accepted[0]) + " blue orders; " + std::to_string(completed) + " builds completed";
        if (result.destroyed[0] + result.destroyed[1]) result.text += "; " + std::to_string(result.destroyed[0] + result.destroyed[1]) + " units destroyed";
        if (blue.missed || red.missed) result.text += "; deadline reached, partial orders retained";
        if (terminal()) result.text += "; " + winner() + " wins";
        return result;
    }
    void checkInvariants() {
        ++invariantChecks_;
        std::set<int> ids, tasks;
        for (const Unit& u : units_) {
            if (!ids.insert(u.id).second || !passable(u.position) || u.hp <= 0 || u.hp > maxHp(u.kind) || u.cargo < 0 || u.cargo > 12 || u.orders.size() > max_orders)
                throw std::logic_error("unit invariant violated");
        }
        for (const auto& b : builds_) {
            const Unit* builder = unit(b.builderId);
            if (!tasks.insert(b.id).second || !builder || builder->side != b.side || b.remaining <= 0 || b.remaining > b.total || b.total <= 0 || !passable(b.position))
                throw std::logic_error("build invariant violated");
            if (b.kind == Kind::Tower && (builder->orders.empty() || builder->orders.front().type != ActionType::Build || builder->orders.front().targetId != b.id))
                throw std::logic_error("builder reservation invariant violated");
        }
        for (Side side : {Side::Blue, Side::Red}) if (resources(side) < 0 || population(side) > max_population) throw std::logic_error("economy invariant violated");
        for (const auto& mine : mines_) if (!inside(mine.position) || mine.remaining < 0) throw std::logic_error("resource invariant violated");
    }
};

// This policy uses only the current side's Observation and its own style. It
// adapts the production order, scout route and attack threshold to sighted foes.
class HeuristicPolicy {
    std::string style_;
    std::uint32_t seed_;
public:
    explicit HeuristicPolicy(std::string style = "balanced", std::uint32_t seed = 1) : style_(std::move(style)), seed_(seed) {
        if (style_ != "balanced" && style_ != "rush" && style_ != "macro") throw std::invalid_argument("unknown policy style");
    }
    const std::string& style() const { return style_; }
    Decision decide(const Observation& view, int limit = 240) const {
        DecisionBudget budget(limit); Decision out; out.budget = budget.limit;
        const auto finish = [&]() { out.used = budget.used; out.missed = budget.exhausted; return out; };
        if (!budget.spend(8)) return finish();
        const UnitView* home = nullptr;
        std::vector<const UnitView*> workers, soldiers, scouts;
        int towers = 0;
        for (const auto& u : view.own) {
            if (!budget.spend(2)) return finish();
            if (u.kind == Kind::Base) home = &u;
            else if (u.kind == Kind::Worker) workers.push_back(&u);
            else if (u.kind == Kind::Soldier) soldiers.push_back(&u);
            else if (u.kind == Kind::Scout) scouts.push_back(&u);
            else if (u.kind == Kind::Tower) ++towers;
        }
        if (!home) return finish();
        int queuedWorkers = 0, queuedSoldiers = 0, queuedScouts = 0, slots = 0;
        for (const auto& b : view.builds) {
            if (!budget.spend(2)) return finish();
            if (b.kind == Kind::Worker) ++queuedWorkers;
            if (b.kind == Kind::Soldier) ++queuedSoldiers;
            if (b.kind == Kind::Scout) ++queuedScouts;
            if (b.kind == Kind::Tower) ++towers;
            if (b.builderId == home->id) ++slots;
        }
        const ResourceView* bestMine = nullptr;
        for (const auto& m : view.mines) {
            if (!budget.spend(3)) return finish();
            if (m.remaining > 0 && (!bestMine || distance(home->position, m.position) < distance(home->position, bestMine->position))) bestMine = &m;
        }
        for (const auto* w : workers) {
            if (!budget.spend(3)) return finish();
            if (!w->busy && bestMine) out.commands.push_back(Command::order(w->id, ActionType::Gather, bestMine->position, bestMine->id));
        }
        int threat = 0; Point attack = view.enemyStart; int nearest = width + height;
        for (const auto& enemy : view.visibleEnemy) {
            if (!budget.spend(4)) return finish();
            const int d = distance(home->position, enemy.position);
            if (d < 10 && (enemy.kind == Kind::Soldier || enemy.kind == Kind::Tower)) ++threat;
            if (d < nearest) { nearest = d; attack = enemy.position; }
        }
        if (view.visibleEnemy.empty()) {
            int latest = -1;
            for (const auto& memory : view.intel) {
                if (!budget.spend(2)) return finish();
                if ((memory.kind == Kind::Base || memory.kind == Kind::Tower) && memory.lastSeen > latest) { latest = memory.lastSeen; attack = memory.position; }
            }
        }
        int money = view.resources;
        // Defense can run in parallel with both base production slots.
        if (threat > 0 && towers < 2 && money >= cost(Kind::Tower)) {
            for (const auto* w : workers) {
                if (!budget.spend(3)) return finish();
                if (w->action == ActionType::Build) continue;
                Point position{home->position.x + (view.side == Side::Blue ? 3 : -3), home->position.y + (towers ? 2 : -2)};
                out.commands.push_back(Command::construct(w->id, position)); money -= cost(Kind::Tower); break;
            }
        }
        const int desiredWorkers = style_ == "macro" ? 7 : style_ == "rush" ? 4 : 5;
        const int desiredScouts = style_ == "rush" ? (view.tick >= 24 ? 1 : 0) : 1;
        int workersPlanned = static_cast<int>(workers.size()) + queuedWorkers;
        int soldiersPlanned = static_cast<int>(soldiers.size()) + queuedSoldiers;
        int scoutsPlanned = static_cast<int>(scouts.size()) + queuedScouts;
        int pop = workersPlanned + soldiersPlanned + scoutsPlanned;
        while (slots < 2 && pop < max_population) {
            if (!budget.spend(8)) return finish();
            Kind next = Kind::Soldier;
            if (scoutsPlanned < desiredScouts && threat == 0) next = Kind::Scout;
            else if (workersPlanned < desiredWorkers && threat == 0 && (style_ != "rush" || soldiersPlanned >= 4)) next = Kind::Worker;
            if (money < cost(next)) break;
            out.commands.push_back(Command::train(home->id, next)); money -= cost(next); ++slots; ++pop;
            if (next == Kind::Worker) ++workersPlanned;
            if (next == Kind::Soldier) ++soldiersPlanned;
            if (next == Kind::Scout) ++scoutsPlanned;
        }
        for (const auto* scout : scouts) {
            if (!budget.spend(8)) return finish();
            if (scout->busy) continue;
            // Alternating flank and enemy-start waypoints reveal builds without
            // accessing hidden enemy positions or resources.
            Point destination = (view.tick / 22 + seed_ % 2) % 2 == 0 ? view.enemyStart : Point{view.side == Side::Blue ? 22 : 9, 13};
            out.commands.push_back(Command::order(scout->id, ActionType::Move, destination));
        }
        const int pushAt = style_ == "rush" ? 3 : style_ == "macro" ? 7 : 5;
        const bool push = static_cast<int>(soldiers.size()) >= pushAt || view.tick >= 95;
        for (const auto* soldier : soldiers) {
            if (!budget.spend(7)) return finish();
            Point destination = threat > 0 ? attack : push ? attack : Point{home->position.x + (view.side == Side::Blue ? 5 : -5), 9};
            // Refresh attack-move goals at a fixed cadence, not every tick.
            if (!soldier->busy || (view.tick % 8 == 0 && threat > 0))
                out.commands.push_back(Command::order(soldier->id, ActionType::AttackMove, destination));
        }
        return finish();
    }
};

} // namespace tempo
#endif
