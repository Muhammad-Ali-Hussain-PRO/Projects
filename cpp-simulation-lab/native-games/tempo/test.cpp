#include "core.hpp"
#include <iostream>
#include <sstream>

using namespace tempo;
namespace {
int checks = 0;
void require(bool condition, const std::string& explanation) {
    ++checks; if (!condition) throw std::runtime_error(explanation);
}
Scenario homes() {
    return {{{Side::Blue, Kind::Base, {3, 9}}, {Side::Red, Kind::Base, {28, 9}}}, {}, {{300, 300}}};
}
const UnitView* findKind(const Observation& v, Kind kind) {
    auto it = std::find_if(v.own.begin(), v.own.end(), [kind](const UnitView& u) { return u.kind == kind; });
    return it == v.own.end() ? nullptr : &*it;
}
std::string digest(const Observation& v) {
    std::ostringstream out; out << v.tick << ':' << v.resources << ':' << v.score;
    for (const auto& u : v.own) out << '|' << u.id << ',' << static_cast<int>(u.kind) << ',' << u.position.x << ',' << u.position.y << ',' << u.hp << ',' << u.cargo << ',' << u.queuedOrders;
    for (const auto& u : v.visibleEnemy) out << '/' << u.id << ',' << u.position.x << ',' << u.position.y << ',' << u.hp;
    for (const auto& b : v.builds) out << '#' << b.id << ',' << b.remaining;
    for (const auto& s : v.intel) out << '?' << s.id << ',' << s.position.x << ',' << s.position.y << ',' << s.lastSeen;
    return out.str();
}
void fogIsolation() {
    auto a = homes(), b = homes(); b.resources[1] = 999;
    a.units.push_back({Side::Red, Kind::Soldier, {25, 8}});
    b.units.push_back({Side::Red, Kind::Scout, {25, 13}});
    Engine left(17, a), right(17, b);
    require(left.observe(Side::Blue).visibleEnemy.empty(), "unseen enemy must be hidden");
    require(digest(left.observe(Side::Blue)) == digest(right.observe(Side::Blue)), "hidden enemy state must not alter blue observation");
    require(left.observe(Side::Red).resources == 300 && right.observe(Side::Red).resources == 999, "each side sees its own economy");
    HeuristicPolicy policy;
    const auto da = policy.decide(left.observe(Side::Blue));
    const auto db = policy.decide(right.observe(Side::Blue));
    require(da.commands.size() == db.commands.size() && da.used == db.used, "policy must not respond to unseen opponent economy");
}
void scoutAndMemory() {
    auto s = homes(); s.units.push_back({Side::Blue, Kind::Scout, {17, 9}});
    Engine e(1, s); auto v = e.observe(Side::Blue);
    require(v.visibleEnemy.empty(), "enemy base starts beyond scout range");
    const int scout = findKind(v, Kind::Scout)->id;
    Decision go; go.commands.push_back(Command::order(scout, ActionType::Move, {24, 9}));
    e.step(go, {}); e.step({}, {}); v = e.observe(Side::Blue);
    require(v.visibleEnemy.size() == 1 && v.visibleEnemy[0].kind == Kind::Base, "scout movement reveals enemy base");
    require(v.intel.size() == 1 && v.intel[0].lastSeen == e.tick(), "visible sighting has current timestamp");
    const Point sighted = v.intel[0].position;
    const int before = static_cast<int>(v.explored.size());
    Decision retreat; retreat.commands.push_back(Command::order(scout, ActionType::Move, {10, 9}));
    for (int i = 0; i < 6; ++i) e.step(i == 0 ? retreat : Decision{}, {});
    v = e.observe(Side::Blue);
    require(v.visibleEnemy.empty(), "retreat restores fog around enemy base");
    require(v.intel.size() == 1 && v.intel[0].position == sighted && v.intel[0].lastSeen < e.tick(), "hidden sighting remains a timestamped snapshot");
    require(static_cast<int>(v.explored.size()) >= before, "exploration is persistent");
}
void queuesAndReservations() {
    Engine e(1, homes()); const int home = findKind(e.observe(Side::Blue), Kind::Base)->id;
    Decision d; d.commands = {Command::train(home, Kind::Worker), Command::train(home, Kind::Soldier), Command::train(home, Kind::Scout)};
    const auto result = e.step(d, {}); auto v = e.observe(Side::Blue);
    require(result.accepted[0] == 2 && result.rejected[0] == 1, "base has exactly two concurrent production slots");
    require(v.resources == 200 && v.builds.size() == 2, "costs reserve atomically at acceptance");
    require(v.builds[0].remaining == 7 && v.builds[1].remaining == 11, "concurrent tasks both advance in one tick");
    for (int i = 0; i < 7; ++i) e.step({}, {});
    v = e.observe(Side::Blue);
    require(findKind(v, Kind::Worker) && v.builds.size() == 1, "shorter build completes independently");
    const int worker = findKind(v, Kind::Worker)->id;
    Decision orders;
    for (int i = 0; i < 5; ++i) orders.commands.push_back(Command::order(worker, ActionType::Move, {8 + i, 9}, -1, true));
    const auto queued = e.step(orders, {}); v = e.observe(Side::Blue);
    require(queued.accepted[0] == 4 && queued.rejected[0] == 1 && findKind(v, Kind::Worker)->queuedOrders == 4, "queued unit actions have a strict cap");
    const Point p = findKind(v, Kind::Worker)->position;
    e.step({}, {}); require(findKind(e.observe(Side::Blue), Kind::Worker)->position != p, "unit orders persist without new decisions");
}
void economyAndStructures() {
    auto s = homes(); s.units.push_back({Side::Blue, Kind::Worker, {5, 9}}); s.mines = {{7, {5, 9}, 100}};
    Engine e(1, s); int worker = findKind(e.observe(Side::Blue), Kind::Worker)->id;
    Decision gather; gather.commands.push_back(Command::order(worker, ActionType::Gather, {5, 9}, 7));
    e.step(gather, {}); e.step({}, {}); e.step({}, {});
    require(e.resources(Side::Blue) == 300 && findKind(e.observe(Side::Blue), Kind::Worker)->cargo == 12, "mining fills cargo before earning spendable resources");
    e.step({}, {}); e.step({}, {});
    require(e.resources(Side::Blue) == 312, "return trip deposits cargo into the economy");
    const int home = findKind(e.observe(Side::Blue), Kind::Base)->id;
    Decision build; build.commands = {Command::construct(worker, {6, 9}), Command::train(home, Kind::Soldier), Command::train(home, Kind::Scout)};
    const auto accepted = e.step(build, {}); auto v = e.observe(Side::Blue);
    require(accepted.accepted[0] == 3 && v.builds.size() == 3, "worker construction runs alongside two production tasks");
    require(v.resources == 127, "parallel reservations deduct exact costs");
    Decision interrupt; interrupt.commands.push_back(Command::order(worker, ActionType::Move, {8, 9}));
    require(e.step(interrupt, {}).rejected[0] == 1, "paid construction reservation cannot be silently interrupted");
    for (int i = 0; i < 25; ++i) e.step({}, {});
    require(findKind(e.observe(Side::Blue), Kind::Tower) != nullptr, "builder reaches site and finishes tower");
}
void simultaneousCombat() {
    auto s = homes(); s.units.push_back({Side::Blue, Kind::Soldier, {15, 9}, 8}); s.units.push_back({Side::Red, Kind::Soldier, {16, 9}, 8});
    Engine e(1, s); const auto result = e.step({}, {});
    require(result.destroyed[0] == 1 && result.destroyed[1] == 1, "lethal attacks resolve simultaneously");
    require(e.winner() == "ongoing", "losing soldiers does not end a match with living bases");
    auto end = homes(); end.units[0].position = {14, 9}; end.units[0].hp = 8; end.units[1].position = {17, 9}; end.units[1].hp = 8;
    Engine draw(1, end); draw.step({}, {});
    require(draw.terminal() && draw.winner() == "draw", "mutual base destruction is a draw");
    const int tick = draw.tick(); draw.step({}, {}); require(draw.tick() == tick, "finished matches cannot advance");
}
void deadlinesAndDeterminism() {
    Engine e(42); HeuristicPolicy policy;
    const auto timedOut = policy.decide(e.observe(Side::Blue), 28);
    require(timedOut.missed && timedOut.used <= timedOut.budget, "work budget cannot be exceeded");
    require(!timedOut.commands.empty(), "useful partial commands survive a deadline");
    e.step(timedOut, {});
    const auto zero = policy.decide(e.observe(Side::Blue), 0);
    require(zero.missed && zero.commands.empty() && zero.used == 0, "zero budget returns immediately without commands");
    Engine first(29), second(29); HeuristicPolicy blue("balanced", 29), red("rush", 29 ^ 0x9e3779b9U);
    for (int t = 0; t < 180 && !first.terminal(); ++t) {
        const auto a = blue.decide(first.observe(Side::Blue)); const auto b = red.decide(first.observe(Side::Red));
        first.step(a, b);
        second.step(blue.decide(second.observe(Side::Blue)), red.decide(second.observe(Side::Red)));
        require(digest(first.observe(Side::Blue)) == digest(second.observe(Side::Blue)), "same seed and actions must produce identical replay states");
    }
    require(first.winner() == second.winner(), "deterministic outcome");
    require(first.invariantChecks() == first.tick() + 1, "each tick checks runtime invariants");
    for (std::uint32_t seed = 0; seed < 15; ++seed) {
        Engine world(seed); HeuristicPolicy a(seed % 2 ? "macro" : "balanced", seed), b(seed % 3 ? "rush" : "macro", seed + 1);
        for (int tick = 0; tick < 220 && !world.terminal(); ++tick)
            world.step(a.decide(world.observe(Side::Blue), seed % 4 ? 240 : 70), b.decide(world.observe(Side::Red), 240));
        require(world.resources(Side::Blue) >= 0 && world.resources(Side::Red) >= 0, "multi-seed economies remain nonnegative");
    }
}
}
int main() {
    try {
        fogIsolation(); scoutAndMemory(); queuesAndReservations(); economyAndStructures(); simultaneousCombat(); deadlinesAndDeterminism();
        std::cout << "{\"project\":\"tempo\",\"passed\":true,\"checks\":" << checks
            << ",\"suites\":[\"fog-isolation\",\"scout-memory\",\"queued-actions-and-reservations\",\"economy-and-parallel-construction\",\"simultaneous-combat\",\"deadlines-and-determinism\"]}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "tempo test failure: " << error.what() << '\n'; return 1; }
}
