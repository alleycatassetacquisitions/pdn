#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <sys/stat.h>

#include "../test_core/soak/soak-world.hpp"

// The long soak: seeds from the environment (see [env:native_soak] in
// platformio.ini). Each seed prints one summary line; a failing one writes its
// action log and findings under SOAK_LOG_DIR and, with SOAK_SHRINK=1, a
// minimised action list that SOAK_REPLAY plays back. SOAK_BLACKOUT_MINUTES=<m>
// is ring-fill mode: every FDN is out of radio range for the first m minutes, so
// PDN rings fill and refuse (give SOAK_MINUTES about m + 15 for the refill).

namespace {

std::string environment(const char* name, const char* fallback) {
    const char* value = getenv(name);
    return value != nullptr && value[0] != '\0' ? value : fallback;
}

// With SOAK_VARY=1 the seed also picks the event's size (PDNs 12-50, FDNs 3-5,
// 6-20 virtual minutes), so a batch covers small and large events at a lower
// average cost. An explicit SOAK_PDNS, SOAK_FDNS or SOAK_MINUTES still wins.
std::string sized(const char* name, std::mt19937_64& rng, int low, int high, const char* fallback) {
    if (environment("SOAK_VARY", "0") != "1" || getenv(name) != nullptr) {
        return environment(name, fallback);
    }
    return std::to_string(std::uniform_int_distribution<int>(low, high)(rng));
}

soak::SoakConfig configFor(uint64_t seed) {
    soak::SoakConfig config;
    config.schedule.seed = seed;
    std::mt19937_64 rng(seed ^ 0x5157E);
    config.schedule.pdns = std::stoi(sized("SOAK_PDNS", rng, 12, 50, "50"));
    config.schedule.fdns = std::stoi(sized("SOAK_FDNS", rng, 3, 5, "4"));
    config.schedule.horizonMs = std::stoull(sized("SOAK_MINUTES", rng, 6, 20, "20")) * 60000;
    config.serverUrl = environment("SOAK_SERVER_URL", "");
    config.http.serverError = 0.05;
    config.http.transportFail = 0.02;
    config.http.lostResponse = 0.03;
    config.schedule.fdnBlackoutMs = std::stoull(environment("SOAK_BLACKOUT_MINUTES", "0")) * 60000;
    return config;
}

std::vector<soak::Action> readActions(const std::string& path) {
    std::vector<soak::Action> actions;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        soak::Action action;
        if (soak::parseAction(line, action)) actions.push_back(action);
    }
    return actions;
}

soak::RunResult play(const soak::SoakConfig& config, const std::vector<soak::Action>& actions) {
    soak::World world(config, actions);
    return world.run();
}

// Drops the tail, then single actions, while the run still fails.
std::vector<soak::Action> shrink(soak::SoakConfig config, std::vector<soak::Action> actions) {
    const auto fails = [&config](const std::vector<soak::Action>& list) {
        config.schedule.horizonMs = list.empty() ? 1000 : list.back().at + 30000;
        return !play(config, list).passed;
    };
    size_t low = 0;
    size_t high = actions.size();
    while (low < high) {
        const size_t middle = (low + high) / 2;
        if (fails(std::vector<soak::Action>(actions.begin(),
                                            actions.begin() + static_cast<long>(middle)))) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    actions.resize(high);
    for (size_t i = actions.size(); i-- > 0;) {
        std::vector<soak::Action> without = actions;
        without.erase(without.begin() + static_cast<long>(i));
        if (fails(without)) actions = without;
    }
    return actions;
}

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream(path) << text;
    printf("soak: wrote %s\n", path.c_str());
}

void report(const soak::SoakConfig& config, const std::vector<soak::Action>& actions,
            const soak::RunResult& result) {
    printf("%s\n", result.summary.c_str());
    if (result.passed) return;
    const std::string dir = environment("SOAK_LOG_DIR", "/home/ubuntu/virtual-event/logs");
    mkdir(dir.c_str(), 0755);
    const std::string stem = dir + "/seed-" + std::to_string(config.schedule.seed);
    std::string findings = result.summary + "\n";
    for (const std::string& failure : result.failures)
        findings += "FAIL " + failure + "\n";
    writeFile(stem + "-findings.txt", findings);
    writeFile(stem + "-log.txt", result.log);
    for (size_t i = 0; i < result.failures.size() && i < 20; i++) {
        printf("  FAIL %s\n", result.failures[i].c_str());
    }
    if (environment("SOAK_SHRINK", "0") != "1") return;
    std::string minimal;
    for (const soak::Action& action : shrink(config, actions))
        minimal += soak::formatAction(action) + "\n";
    writeFile(stem + "-minimal.txt", minimal);
}

}  // namespace

// Test: every seed named by the environment passes the end-to-end checks
TEST(Soak, seedsFromEnvironment) {
    std::vector<uint64_t> seeds;
    const std::string range = environment("SOAK_SEEDS", "");
    if (!range.empty()) {
        const size_t dots = range.find("..");
        const uint64_t first = std::stoull(range.substr(0, dots));
        const uint64_t last =
        dots == std::string::npos ? first : std::stoull(range.substr(dots + 2));
        for (uint64_t seed = first; seed <= last; seed++)
            seeds.push_back(seed);
    } else {
        seeds.push_back(std::stoull(environment("SOAK_SEED", "1")));
    }
    const std::string replay = environment("SOAK_REPLAY", "");
    size_t failed = 0;
    for (uint64_t seed : seeds) {
        const soak::SoakConfig config = configFor(seed);
        std::vector<soak::Action> actions =
        replay.empty() ? soak::Scheduler::generate(config.schedule) : readActions(replay);
        soak::SoakConfig run = config;
        if (!replay.empty())
            run.schedule.horizonMs = actions.empty() ? 1000 : actions.back().at + 30000;
        const soak::RunResult result = play(run, actions);
        report(run, actions, result);
        failed += result.passed ? 0 : 1;
    }
    EXPECT_EQ(failed, 0u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    IdGenerator::initialize(1);
    return RUN_ALL_TESTS();
}
