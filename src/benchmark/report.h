#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>
#include <string_view>
#include "benchmark/settings.h"

namespace vyruz::benchmark {
inline constexpr char protocol[] = "vurus-bench-2";
inline constexpr int warmupSteps = 120, measuredSteps = 1200;
struct Scene {
    const char* id;
    const char* map;
    float x, y;
    bool generatorTour = false;
    bool burnerCrossfire = false;
};
inline constexpr std::array<Scene, 7> scenes{{
    {"cave", "CAVE/T1C2.3DG", 5230, 1080},
    {"generators", "CPU/T3P2.3DG", 0, 0, true},
    {"underground-crowd", "UNDERGROUND/T2U1.3DG", 4870, 2180},
    {"underground-start", "UNDERGROUND/T2U1.3DG", 3968, 348},
    {"hitek-burner", "HITEK/T1H1.3DG", 3670, 2570},
    {"hitek-crowd", "HITEK/T1H1.3DG", 2166, 1107},
    {"burner-crossfire", "HITEK/T2H2.3DG", 4571, 2841, false, true},
}};
struct Stats {
    size_t count = 0;
    double mean = 0, p50 = 0, p95 = 0, p99 = 0, maximum = 0;
};
Stats summarize(std::vector<double> samples);
// Equal scene weighting. 1,000 points = 60 complete benchmark steps/second.
// A step includes one fixed simulation tick and one full rendered/presented frame.
std::optional<unsigned> calculateScore(const std::vector<double>& meanFrameMs);
struct Result {
    std::string id, map, mapFingerprint;
    float x = 0, y = 0;
    size_t nearbyEnemies = 0, initialRows = 0, finalRows = 0, finalLive = 0;
    size_t maxProjectiles = 0;
    bool burnerCrossfire = false;
    size_t maxBurnersFiring = 0, burnerCrossfireSteps = 0;
    std::optional<double> startupRssMiB, sampledPeakRssMiB;
    Stats world, animation, audio, update, render, frame;
};
struct Report {
    std::string startedUtc, build, platform, cpu, gpu, glVersion, audioBackend;
    std::string error, nonstandardReason;
    bool complete = false, standard = true;
    std::string settingsPreset = "default", settingsSource = "fixed-preset";
    GameOptions options = resolveOptions(nullptr, false);
    int drawableWidth = 0, drawableHeight = 0, swapInterval = 0;
    int warmup = warmupSteps, measured = measuredSteps;
    std::vector<Result> results;
    std::optional<unsigned> score() const;
};
std::string jsonReport(const Report&);
std::string htmlReport(const Report&);
bool readReport(std::string_view,Report&,std::string& error);
struct SavedReport {Report report;std::filesystem::path directory;};
std::optional<SavedReport> latestReport(const std::filesystem::path& resultsDirectory);
} // namespace vyruz::benchmark
