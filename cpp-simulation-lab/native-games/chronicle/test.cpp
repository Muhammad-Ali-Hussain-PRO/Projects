#include "core.hpp"
#include <functional>
#include <iostream>

namespace {
std::size_t checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation, const std::string& message) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
chronicle::Result act(chronicle::Engine& engine, const std::string& command, bool expected = true) {
    const std::string before = engine.stateHash();
    const auto previous = engine.state().turn;
    auto result = engine.apply(chronicle::parseCommand(command));
    check(result.accepted == expected, "Unexpected outcome for " + command + ": " + result.code);
    check(engine.validateState(), "Invalid state after " + command);
    check(result.accepted ? engine.state().turn == previous + 1 : engine.stateHash() == before,
          "Transition was not atomic for " + command);
    return result;
}
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
    if (!out) throw std::runtime_error("Test could not write fixture.");
}
std::string withValidChecksum(const std::string& bytes) {
    auto first = bytes.find('\n');
    auto second = bytes.find('\n', first + 1);
    std::string body = bytes.substr(second + 1);
    return "CHRONICLE_SAVE 1\nCHECKSUM " + chronicle::hex64(chronicle::checksum(body)) + "\n" + body;
}
void completeStory(chronicle::Engine& engine) {
    for (std::uint64_t step = 0; step < 80 && (engine.state().miraArc != 2 || engine.state().orinArc != 2); ++step) {
        auto command = chronicle::demoCommand(engine, step);
        auto result = engine.apply(command);
        check(result.accepted, "Story pilot rejected " + chronicle::commandText(command));
        check(engine.validateState(), "Story pilot broke invariants.");
    }
    check(engine.state().miraArc == 2 && engine.state().orinArc == 2, "Both NPC arcs must complete.");
}
}

int main() {
    std::string savePath, corruptPath;
    try {
#if defined(__unix__) || defined(__APPLE__)
        savePath = "/tmp/chronicle-test-" + std::to_string(::getpid()) + ".save";
#else
        savePath = "chronicle-test.save";
#endif
        corruptPath = savePath + ".corrupt";
        for (int from = 0; from < 6; ++from) {
            check(!chronicle::graph()[from].empty(), "A world node has no edges.");
            for (auto next : chronicle::graph()[from]) {
                const int to = static_cast<int>(next);
                check(to >= 0 && to < 6, "World edge leaves the bounded graph.");
                const auto& back = chronicle::graph()[to];
                check(std::find(back.begin(), back.end(), static_cast<chronicle::Location>(from)) != back.end(), "World edge is not symmetric.");
            }
        }
        chronicle::Engine rules(7);
        act(rules, "move moon", false);
        act(rules, "move ruins", false);
        act(rules, "talk orin", false);
        act(rules, "trade potion", false);
        act(rules, "use potion", false);
        act(rules, "move shrine");
        act(rules, "move cavern", false);
        act(rules, "move village");
        const auto malformedHash = rules.stateHash();
        const auto journalSize = rules.eventLog().size();
        check(!rules.apply({static_cast<chronicle::Action>(99), ""}).accepted, "Malformed enum accepted.");
        check(!rules.apply({chronicle::Action::Move, std::string(65, 'x')}).accepted, "Oversized argument accepted.");
        check(!rules.apply({chronicle::Action::Talk, std::string("mi\0ra", 5)}).accepted, "Embedded NUL accepted.");
        check(rules.stateHash() == malformedHash && rules.eventLog().size() == journalSize, "Malformed commands changed state or journal.");
        rejects([] { chronicle::parseCommand("move woods extra"); }, "Parser accepted an extra token.");
        rejects([] { chronicle::parseCommand("teleport ruins"); }, "Parser accepted an unknown action.");
        act(rules, "move woods");
        for (int i = 0; i < 7; ++i) act(rules, "gather herb");
        act(rules, "gather herb", false);
        check(chronicle::inventorySize(rules.state()) == 8, "Inventory capacity was not enforced.");
        act(rules, "move ruins");
        act(rules, "explore", false);
        act(rules, "move woods");
        act(rules, "talk orin");
        act(rules, "move ruins");
        act(rules, "explore");
        check(rules.state().sentinelDefeated, "Sentinel did not clear.");
        check(rules.state().hp >= 16 && rules.state().hp <= 18, "Sentinel damage was out of bounds.");
        chronicle::Engine trade(9);
        act(trade, "move market");
        act(trade, "trade key");
        act(trade, "trade key", false);
        act(trade, "trade potion", false);
        act(trade, "move village");
        act(trade, "move shrine");
        act(trade, "move cavern");
        act(trade, "explore");
        act(trade, "use potion");
        act(trade, "use potion", false);
        check(chronicle::itemCount(trade.state(), chronicle::Item::Ore) == 1, "Cavern reward missing.");

        chronicle::Engine story(42);
        completeStory(story);
        check(story.state().relicTaken && !chronicle::itemCount(story.state(), chronicle::Item::Relic), "Returned relic ownership is inconsistent.");
        while (story.state().location != chronicle::Location::Village) act(story, chronicle::commandText(chronicle::routeTo(story, chronicle::Location::Village)));
        const int settledGold = story.state().gold;
        act(story, "talk mira");
        check(story.state().gold == settledGold, "Completed Mira arc paid twice.");
        check(story.snapshot().npcArc.find("restored archive") != std::string::npos, "Narrator missed completed arc.");

        std::uint64_t longRunTurns = 0;
        std::uint64_t minimumRunTurns = 600;
        for (std::uint64_t seed : {0ULL, 1ULL, 7ULL, 42ULL, 99ULL, 2026ULL, 65535ULL, 18446744073709551615ULL}) {
            chronicle::Engine longRun(seed), duplicate(seed);
            for (std::uint64_t step = 0; step < 600; ++step) {
                auto command = step % 19 == 0 ? chronicle::Command{chronicle::Action::Move, "moon"} : chronicle::demoCommand(longRun, step);
                const auto before = longRun.stateHash();
                const auto previous = longRun.state().turn;
                auto result = longRun.apply(command);
                auto repeat = duplicate.apply(command);
                check(longRun.validateState(), "Long-run state invariant failure.");
                check(longRun.stateHash() == duplicate.stateHash() && result.accepted == repeat.accepted && result.text == repeat.text, "Identical seed and command diverged.");
                check(result.accepted ? longRun.state().turn == previous + 1 : longRun.stateHash() == before, "Long-run transition was not atomic.");
            }
            check(longRun.state().turn >= 300, "Consistency run committed fewer than 300 turns.");
            check(longRun.state().miraArc == 2 && longRun.state().orinArc == 2, "Long-run pilot did not finish NPC arcs.");
            auto replayed = chronicle::Engine::replay(seed, longRun.eventLog());
            check(replayed.stateHash() == longRun.stateHash(), "600-action replay diverged.");
            longRunTurns += longRun.state().turn;
            minimumRunTurns = std::min(minimumRunTurns, longRun.state().turn);
            if (seed == 42) story = longRun;
        }
        story.save(savePath);
        auto loaded = chronicle::Engine::load(savePath);
        check(loaded.stateHash() == story.stateHash(), "Save/load changed state.");
        check(loaded.eventLog().size() == story.eventLog().size(), "Save/load lost events.");
        for (std::uint64_t step = 600; step < 650; ++step) {
            auto command = chronicle::demoCommand(story, step);
            auto expected = story.apply(command);
            auto resumed = loaded.apply(command);
            check(expected.text == resumed.text && story.stateHash() == loaded.stateHash(), "Loaded continuation diverged.");
        }
        loaded.save(savePath); // replace an existing save, not just create a file.
        check(chronicle::Engine::load(savePath).stateHash() == loaded.stateHash(), "Atomic replacement did not persist latest state.");
        const auto original = readFile(savePath);
        auto bad = original;
        bad.back() = bad.back() == 'x' ? 'y' : 'x';
        writeFile(corruptPath, bad);
        rejects([&] { chronicle::Engine::load(corruptPath); }, "Checksum corruption was accepted.");
        bad = original;
        bad.replace(0, std::string("CHRONICLE_SAVE 1").size(), "CHRONICLE_SAVE 2");
        writeFile(corruptPath, bad);
        rejects([&] { chronicle::Engine::load(corruptPath); }, "Unsupported save version was accepted.");
        bad = original;
        const auto stateStart = bad.find("STATE ");
        const auto stateEnd = bad.find('\n', stateStart);
        std::string stateLine = bad.substr(stateStart, stateEnd - stateStart);
        // Change only the serialized RNG, then recompute the checksum. Replay must still reject it.
        std::istringstream tokens(stateLine);
        std::string word;
        std::vector<std::string> fields;
        while (tokens >> word) fields.push_back(word);
        fields[2] = fields[2] == "0" ? "1" : "0";
        std::ostringstream modified;
        for (std::size_t i = 0; i < fields.size(); ++i) modified << (i ? " " : "") << fields[i];
        bad.replace(stateStart, stateEnd - stateStart, modified.str());
        writeFile(corruptPath, withValidChecksum(bad));
        rejects([&] { chronicle::Engine::load(corruptPath); }, "Checksum-valid state/replay mismatch was accepted.");
        writeFile(corruptPath, original.substr(0, 35));
        rejects([&] { chronicle::Engine::load(corruptPath); }, "Truncated save was accepted.");
        auto alteredEvents = loaded.eventLog();
        alteredEvents[10].afterHash = "0000000000000000";
        rejects([&] { chronicle::Engine::replay(loaded.state().seed, alteredEvents); }, "Altered replay hash was accepted.");
        alteredEvents = loaded.eventLog();
        alteredEvents[0].sequence = 2;
        rejects([&] { chronicle::Engine::replay(loaded.state().seed, alteredEvents); }, "Altered replay sequence was accepted.");
        auto invalid = loaded.state();
        invalid.hp = 0;
        check(!chronicle::validState(invalid), "Zero HP state accepted.");
        invalid = loaded.state(); invalid.inventory[static_cast<int>(chronicle::Item::Relic)] = 1;
        check(!chronicle::validState(invalid), "Returned relic duplicate accepted.");
        invalid = loaded.state(); invalid.discovered = 128;
        check(!chronicle::validState(invalid), "Out-of-world discovery accepted.");
        std::remove(savePath.c_str()); std::remove(corruptPath.c_str());
        std::cout << "{\"project\":\"Chronicle Engine\",\"passed\":true,\"checks\":" << checks
                  << ",\"suites\":[\"bounded-world\",\"transactional-rejection\",\"inventory-and-trade\",\"npc-arcs\",\"600-action-consistency\",\"deterministic-replay\",\"durable-save-load\",\"corruption-and-version-rejection\"],"
                  << "\"consistency\":{\"seeds\":8,\"actionsPerSeed\":600,\"committedTurns\":" << longRunTurns
                  << ",\"minimumCommittedTurns\":" << minimumRunTurns << "}}\n";
        return 0;
    } catch (const std::exception& error) {
        if (!savePath.empty()) std::remove(savePath.c_str());
        if (!corruptPath.empty()) std::remove(corruptPath.c_str());
        std::cerr << "Chronicle test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
