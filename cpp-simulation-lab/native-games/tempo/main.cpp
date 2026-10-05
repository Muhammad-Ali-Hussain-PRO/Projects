#include "core.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
std::string quoted(const std::string& value) {
    std::ostringstream out; out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 32) {
            static const char* hex = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else out << c;
    }
    out << '"'; return out.str();
}
void integers(std::ostream& out, const std::vector<int>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
    out << ']';
}
void units(std::ostream& out, const std::vector<tempo::UnitView>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto& u = values[i]; if (i) out << ',';
        out << "{\"id\":" << u.id << ",\"kind\":" << quoted(tempo::name(u.kind))
            << ",\"x\":" << u.position.x << ",\"y\":" << u.position.y << ",\"hp\":" << u.hp
            << ",\"maxHp\":" << tempo::maxHp(u.kind) << ",\"cargo\":" << u.cargo
            << ",\"busy\":" << (u.busy ? "true" : "false") << ",\"queued\":" << u.queuedOrders
            << ",\"action\":" << quoted(tempo::name(u.action)) << '}';
    }
    out << ']';
}
std::string frame(const tempo::Observation& view, const tempo::Decision& decision, const std::string& description) {
    std::ostringstream out;
    out << "{\"tick\":" << view.tick << ",\"resources\":" << view.resources << ",\"own\":"; units(out, view.own);
    out << ",\"visibleEnemy\":"; units(out, view.visibleEnemy);
    out << ",\"builds\":[";
    for (std::size_t i = 0; i < view.builds.size(); ++i) {
        const auto& b = view.builds[i]; if (i) out << ',';
        out << "{\"id\":" << b.id << ",\"kind\":" << quoted(tempo::name(b.kind))
            << ",\"remaining\":" << b.remaining << ",\"total\":" << b.total << ",\"cost\":" << b.reservedCost
            << ",\"builder\":" << b.builderId << ",\"x\":" << b.position.x << ",\"y\":" << b.position.y << '}';
    }
    out << "],\"deadline\":{\"budget\":" << decision.budget << ",\"used\":" << decision.used
        << ",\"missed\":" << (decision.missed ? "true" : "false") << "},\"score\":" << view.score
        << ",\"text\":" << quoted(description) << ",\"map\":{\"width\":" << tempo::width << ",\"height\":" << tempo::height << ",\"blocked\":";
    integers(out, view.blocked); out << "},\"visible\":"; integers(out, view.visible);
    out << ",\"explored\":"; integers(out, view.explored);
    out << ",\"intel\":[";
    for (std::size_t i = 0; i < view.intel.size(); ++i) {
        const auto& s = view.intel[i]; if (i) out << ',';
        out << "{\"id\":" << s.id << ",\"kind\":" << quoted(tempo::name(s.kind)) << ",\"x\":" << s.position.x
            << ",\"y\":" << s.position.y << ",\"hp\":" << s.hp << ",\"lastSeen\":" << s.lastSeen << '}';
    }
    out << "],\"mines\":[";
    for (std::size_t i = 0; i < view.mines.size(); ++i) {
        const auto& m = view.mines[i]; if (i) out << ',';
        out << "{\"id\":" << m.id << ",\"x\":" << m.position.x << ",\"y\":" << m.position.y
            << ",\"remaining\":" << m.remaining << ",\"lastSeen\":" << m.lastSeen << '}';
    }
    out << "]}"; return out.str();
}
long long number(const std::string& input, const std::string& option, long long low, long long high) {
    std::size_t end = 0; long long result;
    try { result = std::stoll(input, &end); } catch (...) { throw std::invalid_argument(option + " requires an integer"); }
    if (end != input.size() || result < low || result > high) throw std::invalid_argument(option + " is outside its supported range");
    return result;
}
}

int main(int argc, char** argv) {
    try {
        std::uint32_t seed = 1; int steps = 180, budget = 240, stride = 1; bool json = false;
        std::string style = "balanced", opponent = "rush";
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--json") { json = true; continue; }
            if (option == "--help" || option == "-h") {
                std::cout << "Tempo RTS\nUsage: tempo [--seed N] [--steps 0..10000] [--budget 0..1000000] [--style balanced|rush|macro] [--opponent balanced|rush|macro] [--frame-stride N] [--json]\n"
                    << "JSON is a blue-side observation replay. Work budgets are deterministic operation units.\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + option);
            const std::string value = argv[++i];
            if (option == "--seed") seed = static_cast<std::uint32_t>(number(value, option, 0, std::numeric_limits<std::uint32_t>::max()));
            else if (option == "--steps") steps = static_cast<int>(number(value, option, 0, 10000));
            else if (option == "--budget") budget = static_cast<int>(number(value, option, 0, 1000000));
            else if (option == "--frame-stride") stride = static_cast<int>(number(value, option, 1, 10000));
            else if (option == "--style") style = value;
            else if (option == "--opponent") opponent = value;
            else throw std::invalid_argument("unknown option " + option);
        }
        tempo::Engine engine(seed); tempo::HeuristicPolicy blue(style, seed), red(opponent, seed ^ 0x9e3779b9U);
        tempo::Decision initial; initial.budget = budget;
        std::vector<std::string> frames;
        if (json) frames.push_back(frame(engine.observe(tempo::Side::Blue), initial, "Blue scouts the map while the opposing build order is hidden."));
        int blueMisses = 0, redMisses = 0, accepted = 0, rejected = 0;
        for (int t = 0; t < steps && !engine.terminal(); ++t) {
            const auto blueDecision = blue.decide(engine.observe(tempo::Side::Blue), budget);
            const auto redDecision = red.decide(engine.observe(tempo::Side::Red), budget);
            blueMisses += blueDecision.missed; redMisses += redDecision.missed;
            const auto result = engine.step(blueDecision, redDecision);
            accepted += result.accepted[0]; rejected += result.rejected[0];
            if (json && (engine.tick() % stride == 0 || t + 1 == steps || engine.terminal()))
                frames.push_back(frame(engine.observe(tempo::Side::Blue), blueDecision, result.text));
        }
        if (json) {
            std::cout << "{\"project\":\"tempo\",\"seed\":" << seed << ",\"mode\":\"heuristic-vs-heuristic\",\"summary\":{\"ticks\":" << engine.tick()
                << ",\"requestedSteps\":" << steps << ",\"winner\":" << quoted(engine.winner()) << ",\"blueStyle\":" << quoted(style)
                << ",\"redStyle\":" << quoted(opponent) << ",\"blueScore\":" << engine.score(tempo::Side::Blue)
                << ",\"redScore\":" << engine.score(tempo::Side::Red) << ",\"blueResources\":" << engine.resources(tempo::Side::Blue)
                << ",\"redResources\":" << engine.resources(tempo::Side::Red) << ",\"deadlineMisses\":" << blueMisses + redMisses
                << ",\"blueDeadlineMisses\":" << blueMisses << ",\"redDeadlineMisses\":" << redMisses
                << ",\"acceptedBlueOrders\":" << accepted << ",\"rejectedBlueOrders\":" << rejected
                << ",\"invariantChecks\":" << engine.invariantChecks() << "},\"frames\":[";
            for (std::size_t i = 0; i < frames.size(); ++i) { if (i) std::cout << ','; std::cout << frames[i]; }
            std::cout << "]}\n";
        } else {
            std::cout << "Tempo seed " << seed << " | " << style << " vs " << opponent << " | " << engine.tick() << " ticks\n"
                << "Winner: " << engine.winner() << "; score blue=" << engine.score(tempo::Side::Blue) << " red=" << engine.score(tempo::Side::Red) << '\n'
                << "Blue resources=" << engine.resources(tempo::Side::Blue) << "; accepted orders=" << accepted << "; rejected orders=" << rejected
                << "; deadline misses=" << blueMisses + redMisses << "; invariant checks=" << engine.invariantChecks() << '\n';
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << "tempo: " << error.what() << '\n'; return 2; }
}
