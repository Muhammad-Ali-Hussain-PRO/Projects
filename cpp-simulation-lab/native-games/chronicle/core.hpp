#ifndef CHRONICLE_ENGINE_CORE_HPP
#define CHRONICLE_ENGINE_CORE_HPP

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace chronicle {

enum class Location : int { Village, Woods, Ruins, Market, Shrine, Cavern, Count };
enum class Item : int { Herb, Potion, Key, Relic, Ore, Count };
enum class Action : int { Move, Gather, Talk, Trade, Rest, Explore, Use, Inspect };

inline const std::array<std::string, 6>& locationNames() {
    static const std::array<std::string, 6> names{{"village", "woods", "ruins", "market", "shrine", "cavern"}};
    return names;
}
inline const std::array<std::string, 5>& itemNames() {
    static const std::array<std::string, 5> names{{"herb", "potion", "key", "relic", "ore"}};
    return names;
}
inline const std::array<std::string, 8>& actionNames() {
    static const std::array<std::string, 8> names{{"move", "gather", "talk", "trade", "rest", "explore", "use", "inspect"}};
    return names;
}
inline std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}
inline std::uint64_t checksum(const std::string& bytes) {
    // FNV-1a is a deterministic corruption check, not an authentication primitive.
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}

struct Command {
    Action action = Action::Inspect;
    std::string argument;
};
inline Command parseCommand(const std::string& line) {
    std::istringstream in(line);
    std::string verb, argument, extra;
    if (!(in >> verb)) throw std::invalid_argument("A command needs an action.");
    in >> argument;
    if (in >> extra) throw std::invalid_argument("A command has at most one argument.");
    const auto& names = actionNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == verb) return {static_cast<Action>(i), argument};
    }
    throw std::invalid_argument("Unknown action: " + verb);
}
inline std::string commandText(const Command& command) {
    const int action = static_cast<int>(command.action);
    if (action < 0 || action >= static_cast<int>(actionNames().size())) return "invalid";
    return actionNames()[action] + (command.argument.empty() ? "" : " " + command.argument);
}

struct State {
    std::uint64_t seed = 1;
    std::uint64_t rng = 1;
    std::uint64_t turn = 0;
    Location location = Location::Village;
    int hp = 20;
    int gold = 10;
    std::array<int, 5> inventory{{0, 1, 0, 0, 0}};
    int miraArc = 0; // 0: needs herb; 1: needs relic; 2: completed.
    int orinArc = 0; // 0: unknown; 1: scouting; 2: sentinel cleared.
    bool sentinelDefeated = false;
    bool relicTaken = false;
    unsigned discovered = 1;
};

inline int inventorySize(const State& state) {
    int size = 0;
    for (int count : state.inventory) size += count;
    return size;
}
inline int itemCount(const State& state, Item item) { return state.inventory[static_cast<int>(item)]; }
inline std::string canonicalState(const State& state) {
    std::ostringstream out;
    out << state.seed << ' ' << state.rng << ' ' << state.turn << ' '
        << static_cast<int>(state.location) << ' ' << state.hp << ' ' << state.gold;
    for (int count : state.inventory) out << ' ' << count;
    out << ' ' << state.miraArc << ' ' << state.orinArc << ' '
        << state.sentinelDefeated << ' ' << state.relicTaken << ' ' << state.discovered;
    return out.str();
}
inline bool validState(const State& state, std::string* error = nullptr) {
    auto fail = [&](const std::string& reason) { if (error) *error = reason; return false; };
    const int location = static_cast<int>(state.location);
    if (location < 0 || location >= 6) return fail("Location is outside the world graph.");
    if (state.hp < 1 || state.hp > 20) return fail("HP is outside [1,20].");
    if (state.gold < 0 || state.gold > 999) return fail("Gold is outside [0,999].");
    for (int count : state.inventory) if (count < 0 || count > 8) return fail("An item count is invalid.");
    if (inventorySize(state) > 8) return fail("Inventory exceeds eight slots.");
    if (itemCount(state, Item::Key) > 1 || itemCount(state, Item::Relic) > 1) return fail("Unique item duplicated.");
    if (state.miraArc < 0 || state.miraArc > 2 || state.orinArc < 0 || state.orinArc > 2) return fail("NPC arc is invalid.");
    if (state.sentinelDefeated && state.orinArc == 0) return fail("Sentinel cleared before meeting Orin.");
    if (state.orinArc == 2 && !state.sentinelDefeated) return fail("Orin completed before sentinel clearance.");
    if (state.relicTaken && (!state.sentinelDefeated || state.miraArc == 0)) return fail("Relic obtained before its quest.");
    if (itemCount(state, Item::Relic) && !state.relicTaken) return fail("Untracked relic in inventory.");
    if (state.relicTaken && state.miraArc == 1 && itemCount(state, Item::Relic) != 1) return fail("Quest relic is missing.");
    if (state.miraArc == 2 && (!state.relicTaken || itemCount(state, Item::Relic) != 0)) return fail("Completed Mira arc has inconsistent relic ownership.");
    if (state.discovered == 0 || (state.discovered & ~63U) || !(state.discovered & 1U)) return fail("Discovery flags are invalid.");
    if (!(state.discovered & (1U << location))) return fail("Current location is undiscovered.");
    if (state.location == Location::Cavern && itemCount(state, Item::Key) == 0) return fail("Cavern entered without a key.");
    return true;
}

inline const std::array<std::vector<Location>, 6>& graph() {
    static const std::array<std::vector<Location>, 6> edges{{
        {Location::Woods, Location::Market, Location::Shrine},
        {Location::Village, Location::Ruins},
        {Location::Woods, Location::Cavern},
        {Location::Village},
        {Location::Village, Location::Cavern},
        {Location::Ruins, Location::Shrine}
    }};
    return edges;
}

struct Result {
    bool accepted = false;
    std::string code;
    std::string text;
};
struct Event {
    std::uint64_t sequence = 0;
    Command command;
    bool accepted = false;
    std::string code;
    std::string beforeHash;
    std::string afterHash;
};
struct Snapshot {
    std::uint64_t turn = 0;
    std::string location;
    int hp = 20;
    int gold = 10;
    std::vector<std::string> inventory;
    std::string npcArc;
    std::string objective;
    std::string text;
};

// This narrator only renders committed state and rule results. It cannot mutate state.
class TemplateNarrator {
public:
    static std::string objective(const State& state) {
        if (state.miraArc == 0) return "Bring a herb from the woods to Mira in the village.";
        if (state.orinArc == 0) return "Ask Orin in the woods about the ruined watchtower.";
        if (!state.sentinelDefeated) return "Explore the ruins and clear the clockwork sentinel.";
        if (!state.relicTaken) return "Explore the cleared ruins and retrieve Mira's relic.";
        if (state.miraArc == 1) return "Return the relic to Mira in the village.";
        if (state.orinArc == 1) return "Tell Orin in the woods that the sentinel is cleared.";
        if (itemCount(state, Item::Key) == 0) return "Buy a key at the market and explore the cavern.";
        return "The village is restored. Explore, trade, and keep the chronicle consistent.";
    }
    static std::string arcs(const State& state) {
        static const std::array<std::string, 3> mira{{"herbal remedy", "lost relic", "restored archive"}};
        static const std::array<std::string, 3> orin{{"stranger", "scout's trust", "watchtower secured"}};
        return "Mira: " + mira[state.miraArc] + "; Orin: " + orin[state.orinArc];
    }
    static std::string describe(const State& state) {
        static const std::array<std::string, 6> scenes{{
            "Lanterns hang above the village archive. Mira is waiting by its door.",
            "Green paths curl through the woods. Orin watches the old watchtower.",
            "The ruins hold a clockwork sentinel and a sealed archive alcove.",
            "The market offers potions and one brass key; herbs and ore can be sold.",
            "Quiet water runs through the shrine. Its stone bench offers free rest.",
            "The brass key opens a cavern rich with blue ore. Shadows demand caution."
        }};
        return scenes[static_cast<int>(state.location)];
    }
};

class Engine {
    State state_;
    std::vector<Event> events_;
    static constexpr std::size_t maxEvents = 100000;
    static std::uint64_t draw(State& state) {
        state.rng += 0x9e3779b97f4a7c15ULL;
        std::uint64_t value = state.rng;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }
    static Result reject(const std::string& code, const std::string& text) { return {false, code, text}; }
    static Result accept(const std::string& code, const std::string& text) { return {true, code, text}; }
    static Result transition(State& state, const Command& command) {
        const std::string& arg = command.argument;
        if (arg.size() > 64 || arg.find_first_of("\r\n\0", 0, 3) != std::string::npos)
            return reject("bad_argument", "The command argument is invalid.");
        auto room = [&]() { return inventorySize(state) < 8; };
        auto rewardFits = [&](int gold) { return state.gold <= 999 - gold; };
        auto add = [&](Item item, int count) { state.inventory[static_cast<int>(item)] += count; };
        switch (command.action) {
        case Action::Move: {
            auto it = std::find(locationNames().begin(), locationNames().end(), arg);
            if (it == locationNames().end()) return reject("unknown_location", "That location is not in this world.");
            Location target = static_cast<Location>(it - locationNames().begin());
            const auto& exits = graph()[static_cast<int>(state.location)];
            if (std::find(exits.begin(), exits.end(), target) == exits.end()) return reject("no_edge", "There is no direct path to " + arg + ".");
            if (target == Location::Cavern && itemCount(state, Item::Key) == 0) return reject("locked", "The cavern gate needs a brass key from the market.");
            state.location = target;
            state.discovered |= 1U << static_cast<int>(target);
            return accept("moved", "You follow the path to the " + arg + ". " + TemplateNarrator::describe(state));
        }
        case Action::Gather:
            if (arg != "herb") return reject("unknown_resource", "Only herbs can be gathered.");
            if (state.location != Location::Woods) return reject("wrong_location", "Herbs grow in the woods.");
            if (!room()) return reject("inventory_full", "Your eight inventory slots are full.");
            add(Item::Herb, 1);
            return accept("gathered", "You gather one fragrant herb from a mossy bank.");
        case Action::Talk:
            if (arg == "mira") {
                if (state.location != Location::Village) return reject("npc_absent", "Mira is in the village.");
                if (state.miraArc == 0 && itemCount(state, Item::Herb)) {
                    if (!rewardFits(6)) return reject("gold_full", "There is no room for Mira's payment.");
                    add(Item::Herb, -1); state.gold += 6; state.miraArc = 1;
                    return accept("mira_remedy", "Mira brews your herb into a remedy and pays six coins. She asks you to recover her archive relic from the ruins.");
                }
                if (state.miraArc == 1 && itemCount(state, Item::Relic)) {
                    if (!rewardFits(20)) return reject("gold_full", "There is no room for Mira's reward.");
                    add(Item::Relic, -1); state.gold += 20; state.miraArc = 2;
                    return accept("mira_complete", "Mira restores the relic to the archive and pays twenty coins. The village's lost history is whole again.");
                }
                return accept("mira_hint", state.miraArc == 0 ? "Mira needs a herb from the woods for a remedy." : state.miraArc == 1 ? "Mira still needs the relic from the ruined watchtower." : "Mira thanks you beside the restored archive.");
            }
            if (arg == "orin") {
                if (state.location != Location::Woods) return reject("npc_absent", "Orin is in the woods.");
                if (state.orinArc == 0) {
                    state.orinArc = 1;
                    return accept("orin_trust", "Orin shares a safe approach to the ruins. Clear its clockwork sentinel, then report back.");
                }
                if (state.orinArc == 1 && state.sentinelDefeated) {
                    if (!rewardFits(8)) return reject("gold_full", "There is no room for Orin's reward.");
                    state.orinArc = 2; state.gold += 8;
                    return accept("orin_complete", "Orin marks the watchtower safe and pays eight coins. His scouting route can reopen.");
                }
                return accept("orin_hint", state.orinArc == 1 ? "Orin waits for word that the ruins are safe." : "Orin greets you as a trusted scout.");
            }
            return reject("unknown_npc", "Only Mira and Orin have arcs in this chronicle.");
        case Action::Trade:
            if (state.location != Location::Market) return reject("wrong_location", "Trading is available at the market.");
            if (arg == "potion" || arg == "key") {
                Item item = arg == "potion" ? Item::Potion : Item::Key;
                int price = arg == "potion" ? 4 : 8;
                if (item == Item::Key && itemCount(state, item)) return reject("unique_item", "You already own the brass key.");
                if (!room()) return reject("inventory_full", "Your eight inventory slots are full.");
                if (state.gold < price) return reject("insufficient_gold", "You cannot afford that item.");
                state.gold -= price; add(item, 1);
                return accept("bought", "You buy one " + arg + " for " + std::to_string(price) + " coins.");
            }
            if (arg == "sell-herb" || arg == "sell-ore") {
                Item item = arg == "sell-herb" ? Item::Herb : Item::Ore;
                int price = item == Item::Herb ? 2 : 5;
                if (!itemCount(state, item)) return reject("missing_item", "You have none of that item to sell.");
                if (!rewardFits(price)) return reject("gold_full", "Your coin purse is full.");
                add(item, -1); state.gold += price;
                return accept("sold", "You sell one " + itemNames()[static_cast<int>(item)] + " for " + std::to_string(price) + " coins.");
            }
            return reject("unknown_trade", "Available trades: potion, key, sell-herb, sell-ore.");
        case Action::Rest:
            if (!arg.empty()) return reject("bad_argument", "Rest takes no argument.");
            if (state.location == Location::Village || state.location == Location::Shrine) {
                state.hp = 20;
                return accept("rested", "You rest in safety and recover to twenty health.");
            }
            if (state.gold < 2) return reject("insufficient_gold", "A guarded camp costs two coins; village and shrine rest is free.");
            state.gold -= 2; state.hp = std::min(20, state.hp + 6);
            return accept("camped", "A guarded camp costs two coins and restores up to six health.");
        case Action::Explore:
            if (!arg.empty()) return reject("bad_argument", "Explore takes no argument.");
            if (state.location == Location::Woods) {
                if (!room()) return reject("inventory_full", "Your inventory is full.");
                add(Item::Herb, 1);
                return accept("woodland_find", "You find a fresh herb beside an old scout's trail.");
            }
            if (state.location == Location::Ruins) {
                if (state.orinArc == 0) return reject("needs_guide", "Ask Orin in the woods for a safe approach first.");
                if (state.hp < 6) return reject("unsafe_health", "You need at least six health to explore the ruins.");
                if (!state.sentinelDefeated) {
                    if (!rewardFits(5)) return reject("gold_full", "Your coin purse cannot hold the sentinel's salvage.");
                    int damage = 2 + static_cast<int>(draw(state) % 3);
                    state.hp -= damage; state.gold += 5; state.sentinelDefeated = true;
                    return accept("sentinel_cleared", "You disable the clockwork sentinel, lose " + std::to_string(damage) + " health, and recover five coins.");
                }
                if (!state.relicTaken && state.miraArc == 1) {
                    if (!room()) return reject("inventory_full", "Make room before retrieving Mira's relic.");
                    state.hp -= 1; add(Item::Relic, 1); state.relicTaken = true;
                    return accept("relic_found", "You open the archive alcove and recover Mira's relic, taking one scratch.");
                }
                if (!rewardFits(4)) return reject("gold_full", "Your coin purse is too full to search for salvage.");
                int coins = 2 + static_cast<int>(draw(state) % 3);
                state.hp -= 1; state.gold += coins;
                return accept("ruin_salvage", "Among the ruined stones you find " + std::to_string(coins) + " coins and take one scratch.");
            }
            if (state.location == Location::Cavern) {
                if (state.hp < 6) return reject("unsafe_health", "You need at least six health to explore the cavern.");
                if (!room()) return reject("inventory_full", "Make room before mining ore.");
                int damage = 1 + static_cast<int>(draw(state) % 3);
                state.hp -= damage; add(Item::Ore, 1);
                return accept("ore_found", "You mine one blue ore and lose " + std::to_string(damage) + " health in the rough passage.");
            }
            return accept("surveyed", TemplateNarrator::describe(state));
        case Action::Use:
            if (arg != "potion") return reject("unknown_use", "Only potions can be used directly.");
            if (!itemCount(state, Item::Potion)) return reject("missing_item", "You have no potion.");
            if (state.hp == 20) return reject("already_healthy", "You are already at full health.");
            add(Item::Potion, -1); state.hp = std::min(20, state.hp + 8);
            return accept("potion_used", "You drink a potion and restore up to eight health.");
        case Action::Inspect:
            if (!arg.empty()) return reject("bad_argument", "Inspect takes no argument.");
            return accept("inspected", TemplateNarrator::describe(state));
        }
        return reject("unknown_action", "That action does not exist.");
    }
    static void durableReplace(const std::string& path, const std::string& bytes) {
        std::string temporary = path + ".tmp";
#if defined(__unix__) || defined(__APPLE__)
        std::string pattern = path + ".tmp.XXXXXX";
        std::vector<char> tempName(pattern.begin(), pattern.end());
        tempName.push_back('\0');
        int fd = ::mkstemp(tempName.data());
        if (fd < 0) throw std::runtime_error("Cannot open save temporary file: " + std::string(std::strerror(errno)));
        temporary = tempName.data();
        std::size_t written = 0;
        while (written < bytes.size()) {
            const ssize_t amount = ::write(fd, bytes.data() + written, bytes.size() - written);
            if (amount < 0 && errno == EINTR) continue;
            if (amount <= 0) { int saved = errno; ::close(fd); std::remove(temporary.c_str()); throw std::runtime_error("Cannot write save: " + std::string(std::strerror(saved))); }
            written += static_cast<std::size_t>(amount);
        }
        if (::fsync(fd) != 0) { int saved = errno; ::close(fd); std::remove(temporary.c_str()); throw std::runtime_error("Cannot sync save: " + std::string(std::strerror(saved))); }
        if (::close(fd) != 0) { std::remove(temporary.c_str()); throw std::runtime_error("Cannot close save."); }
#else
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << bytes; out.flush();
        if (!out) throw std::runtime_error("Cannot write save.");
        out.close();
#endif
        if (std::rename(temporary.c_str(), path.c_str()) != 0) { std::remove(temporary.c_str()); throw std::runtime_error("Cannot atomically replace save: " + std::string(std::strerror(errno))); }
#if defined(__unix__) || defined(__APPLE__)
        const auto slash = path.find_last_of('/');
        const std::string parent = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
        int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0) throw std::runtime_error("Cannot open save directory for sync.");
        int syncResult = ::fsync(directory);
        ::close(directory);
        if (syncResult != 0) throw std::runtime_error("Cannot sync save directory.");
#endif
    }
public:
    explicit Engine(std::uint64_t seed = 1) {
        state_.seed = seed; state_.rng = seed ^ 0xd1b54a32d192ed03ULL;
    }
    const State& state() const { return state_; }
    const std::vector<Event>& eventLog() const { return events_; }
    bool validateState(std::string* error = nullptr) const { return validState(state_, error); }
    std::string stateHash() const { return hex64(checksum(canonicalState(state_))); }
    Result apply(const Command& command) {
        const int action = static_cast<int>(command.action);
        if (action < 0 || action > 7 || command.argument.size() > 64 || command.argument.find_first_of("\r\n\0", 0, 3) != std::string::npos)
            return reject("malformed_command", "Use a known action and one argument of at most 64 bytes without line breaks.");
        if (events_.size() >= maxEvents) throw std::runtime_error("Event log limit reached; start a new chronicle.");
        const std::string before = stateHash();
        State trial = state_;
        Result result = transition(trial, command);
        if (result.accepted) {
            if (trial.turn == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Turn counter overflow.");
            ++trial.turn;
            std::string reason;
            if (!validState(trial, &reason)) throw std::logic_error("Rule produced invalid state: " + reason);
        }
        const std::string after = hex64(checksum(canonicalState(result.accepted ? trial : state_)));
        events_.push_back({static_cast<std::uint64_t>(events_.size() + 1), command, result.accepted, result.code, before, after});
        if (result.accepted) state_ = trial;
        return result;
    }
    Snapshot snapshot(const std::string& text = "") const {
        Snapshot frame;
        frame.turn = state_.turn; frame.location = locationNames()[static_cast<int>(state_.location)];
        frame.hp = state_.hp; frame.gold = state_.gold;
        for (std::size_t i = 0; i < state_.inventory.size(); ++i)
            for (int n = 0; n < state_.inventory[i]; ++n) frame.inventory.push_back(itemNames()[i]);
        frame.npcArc = TemplateNarrator::arcs(state_); frame.objective = TemplateNarrator::objective(state_);
        frame.text = text.empty() ? TemplateNarrator::describe(state_) : text;
        return frame;
    }
    static Engine replay(std::uint64_t seed, const std::vector<Event>& events) {
        if (events.size() > maxEvents) throw std::runtime_error("Replay exceeds event limit.");
        Engine engine(seed);
        for (const auto& event : events) {
            if (event.sequence != engine.events_.size() + 1 || event.beforeHash != engine.stateHash()) throw std::runtime_error("Replay event sequence or before-state mismatch.");
            Result result = engine.apply(event.command);
            if (engine.events_.size() != event.sequence || result.accepted != event.accepted || result.code != event.code || engine.stateHash() != event.afterHash)
                throw std::runtime_error("Replay outcome or after-state mismatch.");
        }
        return engine;
    }
    void save(const std::string& path) const {
        if (path.empty()) throw std::invalid_argument("Save path is empty.");
        std::ostringstream payload;
        payload << "STATE " << canonicalState(state_) << '\n' << "EVENTS " << events_.size() << '\n';
        for (const auto& event : events_) {
            payload << "EVENT " << event.sequence << ' ' << static_cast<int>(event.command.action) << ' '
                    << std::quoted(event.command.argument) << ' ' << event.accepted << ' ' << std::quoted(event.code)
                    << ' ' << event.beforeHash << ' ' << event.afterHash << '\n';
        }
        const std::string body = payload.str();
        durableReplace(path, "CHRONICLE_SAVE 1\nCHECKSUM " + hex64(checksum(body)) + "\n" + body);
    }
    static Engine load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open save file.");
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 0 || size > 32 * 1024 * 1024) throw std::runtime_error("Save file exceeds the 32 MiB limit.");
        file.seekg(0);
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto first = bytes.find('\n');
        const auto second = first == std::string::npos ? std::string::npos : bytes.find('\n', first + 1);
        if (first == std::string::npos || second == std::string::npos) throw std::runtime_error("Incomplete save header.");
        if (bytes.substr(0, first) != "CHRONICLE_SAVE 1") throw std::runtime_error("Unsupported save version.");
        const std::string body = bytes.substr(second + 1);
        if (bytes.substr(first + 1, second - first - 1) != "CHECKSUM " + hex64(checksum(body))) throw std::runtime_error("Save checksum mismatch.");
        std::istringstream in(body);
        State saved;
        std::string tag;
        int location, sentinel, relic;
        if (!(in >> tag) || tag != "STATE" || !(in >> saved.seed >> saved.rng >> saved.turn >> location >> saved.hp >> saved.gold)) throw std::runtime_error("Malformed saved state.");
        saved.location = static_cast<Location>(location);
        for (int& count : saved.inventory) if (!(in >> count)) throw std::runtime_error("Malformed saved inventory.");
        if (!(in >> saved.miraArc >> saved.orinArc >> sentinel >> relic >> saved.discovered) || sentinel < 0 || sentinel > 1 || relic < 0 || relic > 1) throw std::runtime_error("Malformed saved story state.");
        saved.sentinelDefeated = sentinel != 0; saved.relicTaken = relic != 0;
        std::string reason;
        if (!validState(saved, &reason)) throw std::runtime_error("Invalid saved state: " + reason);
        std::size_t count;
        if (!(in >> tag >> count) || tag != "EVENTS" || count > maxEvents) throw std::runtime_error("Invalid saved event count.");
        std::vector<Event> events;
        events.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            Event event;
            int action, accepted;
            if (!(in >> tag >> event.sequence >> action >> std::quoted(event.command.argument) >> accepted >> std::quoted(event.code) >> event.beforeHash >> event.afterHash) || tag != "EVENT" || action < 0 || action > 7 || accepted < 0 || accepted > 1)
                throw std::runtime_error("Malformed saved event.");
            event.command.action = static_cast<Action>(action); event.accepted = accepted != 0;
            if (event.command.argument.size() > 64 || event.code.size() > 64 || event.beforeHash.size() != 16 || event.afterHash.size() != 16) throw std::runtime_error("Saved event field exceeds its bound.");
            events.push_back(event);
        }
        if (in >> tag) throw std::runtime_error("Unexpected trailing save data.");
        Engine reconstructed = replay(saved.seed, events);
        if (canonicalState(reconstructed.state_) != canonicalState(saved)) throw std::runtime_error("Saved state does not match event replay.");
        return reconstructed;
    }
};

inline Command routeTo(const Engine& engine, Location goal) {
    const State& state = engine.state();
    if (state.location == goal) return {Action::Inspect, ""};
    std::array<int, 6> parent{{-1, -1, -1, -1, -1, -1}};
    std::queue<Location> frontier;
    const int start = static_cast<int>(state.location);
    parent[start] = start; frontier.push(state.location);
    while (!frontier.empty()) {
        Location here = frontier.front(); frontier.pop();
        for (Location next : graph()[static_cast<int>(here)]) {
            int index = static_cast<int>(next);
            if (parent[index] != -1 || (next == Location::Cavern && !itemCount(state, Item::Key))) continue;
            parent[index] = static_cast<int>(here); frontier.push(next);
        }
    }
    int next = static_cast<int>(goal);
    if (parent[next] == -1) return {Action::Inspect, ""};
    while (parent[next] != start) next = parent[next];
    return {Action::Move, locationNames()[next]};
}

// A reproducible scripted pilot for CLI/visual demonstrations; it is not an LLM.
inline Command demoCommand(const Engine& engine, std::uint64_t step) {
    const State& state = engine.state();
    auto at = [&](Location target, Command command) { return state.location == target ? command : routeTo(engine, target); };
    if (state.hp < 6) {
        if (itemCount(state, Item::Potion)) return {Action::Use, "potion"};
        return at(Location::Village, {Action::Rest, ""});
    }
    if (state.miraArc == 0) {
        if (!itemCount(state, Item::Herb)) return at(Location::Woods, {Action::Gather, "herb"});
        return at(Location::Village, {Action::Talk, "mira"});
    }
    if (state.orinArc == 0) return at(Location::Woods, {Action::Talk, "orin"});
    if (!state.sentinelDefeated || !state.relicTaken) return at(Location::Ruins, {Action::Explore, ""});
    if (state.miraArc == 1) return at(Location::Village, {Action::Talk, "mira"});
    if (state.orinArc == 1) return at(Location::Woods, {Action::Talk, "orin"});
    if (!itemCount(state, Item::Key)) return at(Location::Market, {Action::Trade, "key"});
    if (inventorySize(state) == 8) {
        if (itemCount(state, Item::Ore)) return at(Location::Market, {Action::Trade, "sell-ore"});
        if (itemCount(state, Item::Herb)) return at(Location::Market, {Action::Trade, "sell-herb"});
        if (state.hp < 20 && itemCount(state, Item::Potion)) return {Action::Use, "potion"};
    }
    // Deliberate invalid actions demonstrate atomic rule rejection once arcs are done.
    if (step % 29 == 0) return {Action::Move, "moon"};
    std::uint64_t choice = checksum(std::to_string(state.seed) + ":" + std::to_string(step)) % 6;
    if (choice == 0) return {Action::Explore, ""};
    if (choice == 1 && state.hp < 15) return {Action::Rest, ""};
    if (choice == 2 && state.location == Location::Market) {
        if (itemCount(state, Item::Ore)) return {Action::Trade, "sell-ore"};
        if (itemCount(state, Item::Herb)) return {Action::Trade, "sell-herb"};
        if (state.gold >= 4 && inventorySize(state) < 6) return {Action::Trade, "potion"};
    }
    if (choice == 3 && state.location == Location::Woods) return {Action::Talk, "orin"};
    if (choice == 4 && state.location == Location::Village) return {Action::Talk, "mira"};
    const auto& exits = graph()[static_cast<int>(state.location)];
    return {Action::Move, locationNames()[static_cast<int>(exits[(checksum(std::to_string(step) + ":path") ^ state.seed) % exits.size()])]};
}

} // namespace chronicle
#endif
