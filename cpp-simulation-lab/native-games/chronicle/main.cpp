#include "core.hpp"
#include <cstdlib>
#include <iostream>

namespace {
std::string jsonString(const std::string& input) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : input) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else out << c;
        }
    }
    out << '"';
    return out.str();
}
void printFrame(const chronicle::Snapshot& frame) {
    std::cout << "{\"turn\":" << frame.turn << ",\"location\":" << jsonString(frame.location)
              << ",\"hp\":" << frame.hp << ",\"gold\":" << frame.gold << ",\"inventory\":[";
    for (std::size_t i = 0; i < frame.inventory.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << jsonString(frame.inventory[i]);
    }
    std::cout << "],\"npcArc\":" << jsonString(frame.npcArc)
              << ",\"objective\":" << jsonString(frame.objective) << ",\"text\":" << jsonString(frame.text) << '}';
}
std::uint64_t number(const std::string& text, const std::string& option) {
    if (text.empty() || text[0] == '-') throw std::invalid_argument(option + " needs a nonnegative integer.");
    std::size_t parsed = 0;
    std::uint64_t value = std::stoull(text, &parsed, 10);
    if (parsed != text.size()) throw std::invalid_argument(option + " needs an integer.");
    return value;
}
void help() {
    std::cout << "Chronicle Engine — deterministic rules with template narration\n"
              << "Usage: chronicle [--seed N] [--steps N] [--json] [--save PATH] [--load PATH]\n"
              << "       chronicle --replay PATH [--json]\n"
              << "       chronicle [--seed N] --command 'move woods' --command 'talk orin'\n"
              << "       chronicle --interactive [--load PATH] [--save PATH]\n"
              << "Actions: move LOCATION; gather herb; talk mira|orin; trade potion|key|sell-herb|sell-ore;\n"
              << "         rest; explore; use potion; inspect. Interactive: save PATH, quit.\n"
              << "--steps counts attempted actions (0..10000); JSON keeps at most 101 sampled frames.\n";
}
}

int main(int argc, char** argv) {
    try {
        std::uint64_t seed = 42, steps = 48;
        bool json = false, interactive = false;
        std::string savePath, loadPath, replayPath;
        std::vector<chronicle::Command> commands;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + arg);
                return argv[++i];
            };
            if (arg == "--seed") seed = number(value(), arg);
            else if (arg == "--steps") steps = number(value(), arg);
            else if (arg == "--json") json = true;
            else if (arg == "--interactive") interactive = true;
            else if (arg == "--save") savePath = value();
            else if (arg == "--load") loadPath = value();
            else if (arg == "--replay") replayPath = value();
            else if (arg == "--command") commands.push_back(chronicle::parseCommand(value()));
            else if (arg == "--help" || arg == "-h") { help(); return 0; }
            else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (steps > 10000) throw std::invalid_argument("--steps is limited to 10000.");
        if (interactive && json) throw std::invalid_argument("--interactive and --json cannot be combined.");
        if (!replayPath.empty() && (!loadPath.empty() || interactive || !commands.empty())) throw std::invalid_argument("--replay cannot be combined with load, interactive, or commands.");
        if (interactive && !commands.empty()) throw std::invalid_argument("--interactive and --command cannot be combined.");
        chronicle::Engine engine = loadPath.empty() ? chronicle::Engine(seed) : chronicle::Engine::load(loadPath);
        std::string mode = interactive ? "interactive-template" : commands.empty() ? "deterministic-demo" : "scripted-commands";
        if (!replayPath.empty()) {
            chronicle::Engine saved = chronicle::Engine::load(replayPath);
            engine = chronicle::Engine::replay(saved.state().seed, saved.eventLog());
            mode = "event-replay";
            steps = 0;
        }
        seed = engine.state().seed;
        std::vector<chronicle::Snapshot> frames{engine.snapshot()};
        const std::uint64_t attempts = commands.empty() ? steps : commands.size();
        const std::uint64_t pilotOffset = engine.eventLog().size();
        const std::uint64_t stride = std::max<std::uint64_t>(1, (attempts + 99) / 100);
        if (interactive) {
            std::cout << "Chronicle Engine. Templates narrate; deterministic rules own all state.\n"
                      << frames.front().text << "\nObjective: " << frames.front().objective << "\n";
            std::string line;
            while (std::cout << "> " && std::getline(std::cin, line)) {
                if (line == "quit" || line == "exit") break;
                if (line.rfind("save ", 0) == 0) {
                    try { engine.save(line.substr(5)); std::cout << "Chronicle saved.\n"; }
                    catch (const std::exception& error) { std::cout << "Save failed: " << error.what() << '\n'; }
                    continue;
                }
                try {
                    auto result = engine.apply(chronicle::parseCommand(line));
                    auto frame = engine.snapshot(result.text);
                    std::cout << (result.accepted ? "" : "Rejected: ") << result.text << "\n"
                              << "Turn " << frame.turn << " | " << frame.location << " | HP " << frame.hp
                              << " | Gold " << frame.gold << " | Inventory " << frame.inventory.size() << "/8\n"
                              << "Objective: " << frame.objective << "\n";
                } catch (const std::exception& error) { std::cout << error.what() << '\n'; }
            }
            frames.push_back(engine.snapshot());
        } else {
            for (std::uint64_t i = 0; i < attempts; ++i) {
                auto command = commands.empty() ? chronicle::demoCommand(engine, pilotOffset + i) : commands[i];
                auto result = engine.apply(command);
                std::string text = result.accepted ? result.text : "Rejected (" + result.code + "): " + result.text;
                if ((i + 1) % stride == 0 || i + 1 == attempts) frames.push_back(engine.snapshot(text));
                if (!json) std::cout << '[' << engine.state().turn << "] " << chronicle::commandText(command) << ": " << text << '\n';
            }
        }
        if (!savePath.empty()) engine.save(savePath);
        std::size_t accepted = 0;
        for (const auto& event : engine.eventLog()) if (event.accepted) ++accepted;
        const int completed = (engine.state().miraArc == 2) + (engine.state().orinArc == 2);
        if (json) {
            std::cout << "{\"project\":\"Chronicle Engine\",\"seed\":" << seed << ",\"mode\":" << jsonString(mode)
                      << ",\"summary\":{\"turns\":" << engine.state().turn << ",\"accepted\":" << accepted
                      << ",\"rejected\":" << engine.eventLog().size() - accepted << ",\"stateHash\":" << jsonString(engine.stateHash())
                      << ",\"completedArcs\":" << completed << "},\"frames\":[";
            for (std::size_t i = 0; i < frames.size(); ++i) { if (i) std::cout << ','; printFrame(frames[i]); }
            std::cout << "]}\n";
        } else if (!interactive) {
            std::cout << "State " << engine.stateHash() << " | " << engine.state().turn << " committed turns | "
                      << completed << "/2 NPC arcs complete | " << engine.eventLog().size() - accepted << " rejected actions\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "chronicle: " << error.what() << '\n';
        return 1;
    }
}
