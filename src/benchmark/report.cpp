#include "benchmark/report.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace vyruz::benchmark {
Stats summarize(std::vector<double> samples) {
    Stats result;
    if (samples.empty()) return result;
    for (double value : samples)
        if (!std::isfinite(value) || value <= 0)
            throw std::invalid_argument("Invalid benchmark timing sample");
    result.count = samples.size();
    result.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double p) {
        return samples[std::min(samples.size() - 1, size_t(samples.size() * p))];
    };
    result.p50 = percentile(.50); result.p95 = percentile(.95);
    result.p99 = percentile(.99); result.maximum = samples.back();
    return result;
}
std::optional<unsigned> calculateScore(const std::vector<double>& means) {
    if (means.empty()) return std::nullopt;
    double logarithms = 0;
    for (double value : means) {
        if (!std::isfinite(value) || value <= 0) return std::nullopt;
        logarithms += std::log(value);
    }
    const double score = 1000.0 * (1000.0 / 60.0) / std::exp(logarithms / means.size());
    if (!std::isfinite(score) || score > std::numeric_limits<unsigned>::max() - .5)
        return std::nullopt;
    return static_cast<unsigned>(std::floor(score + .5));
}
std::optional<unsigned> Report::score() const {
    if (!complete || !standard || results.size() != scenes.size() ||
        warmup != warmupSteps || measured != measuredSteps) return std::nullopt;
    std::vector<double> means;
    for (size_t i = 0; i < scenes.size(); ++i) {
        if (results[i].id != scenes[i].id || results[i].frame.count != size_t(measured) ||
            results[i].burnerCrossfire != scenes[i].burnerCrossfire ||
            (scenes[i].burnerCrossfire && (results[i].maxBurnersFiring < 2 || !results[i].burnerCrossfireSteps)))
            return std::nullopt;
        means.push_back(results[i].frame.mean);
    }
    return calculateScore(means);
}
namespace {
std::string jsonString(const std::string& text) {
    std::ostringstream out; out << '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
            else out << char(c);
        }
    }
    out << '"'; return out.str();
}
std::string htmlString(const std::string& text) {
    std::string result;
    for (char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c;
        }
    }
    return result;
}
void metric(std::ostream& out, const Stats& s) {
    out << "{\"samples\":" << s.count << ",\"mean_ms\":" << s.mean
        << ",\"p50_ms\":" << s.p50 << ",\"p95_ms\":" << s.p95
        << ",\"p99_ms\":" << s.p99 << ",\"max_ms\":" << s.maximum << '}';
}
void optionalNumber(std::ostream& out, std::optional<double> value) {
    if (value && std::isfinite(*value)) out << *value; else out << "null";
}
std::string label(const Result& r) {
    std::ostringstream out; out << r.map << " (" << r.x << ", " << r.y << ')'; return out.str();
}
}
std::string jsonReport(const Report& r) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(10);
    out << "{\n\"schema_version\":3,\"protocol\":" << jsonString(protocol)
        << ",\"started_utc\":" << jsonString(r.startedUtc) << ",\"build\":" << jsonString(r.build)
        << ",\"complete\":" << (r.complete ? "true" : "false")
        << ",\"standard\":" << (r.standard ? "true" : "false") << ",\"score\":";
    if (auto score = r.score()) out << *score; else out << "null";
    out << ",\"score_rule\":\"1000 points at 60 steps/s; geometric mean of per-scene mean frame throughput\""
        << ",\"error\":" << jsonString(r.error) << ",\"nonstandard_reason\":" << jsonString(r.nonstandardReason)
        << ",\n\"device\":{\"platform\":" << jsonString(r.platform) << ",\"cpu\":" << jsonString(r.cpu)
        << ",\"gpu\":" << jsonString(r.gpu) << ",\"gl_version\":" << jsonString(r.glVersion)
        << ",\"audio_backend\":" << jsonString(r.audioBackend)
        << ",\"drawable_width\":" << r.drawableWidth << ",\"drawable_height\":" << r.drawableHeight
        << ",\"swap_interval\":" << r.swapInterval << "},\n"
        << "\"conditions\":{\"settings_preset\":" << jsonString(r.settingsPreset)
        << ",\"settings_source\":" << jsonString(r.settingsSource)
        << ",\"width\":" << r.options.width << ",\"height\":" << r.options.height
        << ",\"window_mode\":" << r.options.windowMode << ",\"gamma\":" << r.options.gamma
        << ",\"hud_aspect\":" << r.options.hudAspect
        << ",\"vsync_requested\":false,\"frame_cap\":0,"
        << "\"fixed_dt_seconds\":0.01666666667,\"frames_per_tick\":1,\"warmup_steps\":" << r.warmup
        << ",\"measured_steps\":" << r.measured
        << ",\"difficulty\":1,\"filtering\":" << r.options.spriteFiltering;
    const auto flag = [&](const char* name, bool value) { out << ',' << jsonString(name) << ':' << (value ? "true" : "false"); };
    flag("shadows", r.options.shadows); flag("reactor", r.options.reactorFx);
    flag("vignette", r.options.vignette); flag("ground_details", r.options.groundDetails);
    flag("crt", r.options.crt); flag("crt_retro", r.options.crtRetro); flag("crt_bright", r.options.crtBright);
    flag("fxaa", r.options.fxaa); flag("smaa", r.options.smaa);
    flag("bloom", r.options.bloom); flag("sharpen", r.options.sharpen);
    out << ",\"music\":false,\"god_mode\":true,\"player_fire\":\"held except Burner crossfire\"},\n\"scenes\":[\n";
    for (size_t i = 0; i < r.results.size(); ++i) {
        const auto& s = r.results[i]; if (i) out << ",\n";
        out << "{\"id\":" << jsonString(s.id) << ",\"map\":" << jsonString(s.map)
            << ",\"map_fingerprint_fnv1a64\":" << jsonString(s.mapFingerprint)
            << ",\"x\":" << s.x << ",\"y\":" << s.y << ",\"nearby_enemies_at_start\":" << s.nearbyEnemies
            << ",\"nearby_radius\":1000,\"initial_rows\":" << s.initialRows
            << ",\"final_rows\":" << s.finalRows << ",\"final_live\":" << s.finalLive
            << ",\"max_projectiles\":" << s.maxProjectiles
            << ",\"burner_crossfire\":" << (s.burnerCrossfire ? "true" : "false")
            << ",\"max_burners_firing_recent_100ms\":" << s.maxBurnersFiring
            << ",\"steps_with_two_burners_firing\":" << s.burnerCrossfireSteps << ",\"startup_rss_mib\":";
        optionalNumber(out, s.startupRssMiB); out << ",\"sampled_peak_rss_mib\":"; optionalNumber(out, s.sampledPeakRssMiB);
        out << ",\"benchmark_fps\":"; optionalNumber(out, s.frame.mean > 0 ? std::optional<double>(1000 / s.frame.mean) : std::nullopt);
        for (const auto& [name, stats] : std::array<std::pair<const char*, const Stats*>, 6>{{
            {"world", &s.world}, {"animation", &s.animation}, {"audio", &s.audio},
            {"cpu_update_total", &s.update}, {"render_present", &s.render}, {"frame", &s.frame}}}) {
            out << ',' << jsonString(name) << ':'; metric(out, *stats);
        }
        out << '}';
    }
    out << "\n]}\n"; return out.str();
}
std::string htmlReport(const Report& r) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(2);
    out << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        << "<title>Vurus benchmark</title><style>body{font:16px/1.5 system-ui,sans-serif;background:#101617;color:#e6efec;max-width:1050px;margin:40px auto;padding:0 18px}"
        << "h1{margin-bottom:0}.score{font-size:52px;color:#a2ed8c;font-weight:700;margin:12px 0}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;margin:20px 0}"
        << "th,td{padding:10px;text-align:right;border-bottom:1px solid #35403c}th:first-child,td:first-child{text-align:left}small,.note{color:#b3c2ba}a{color:#a2ed8c}</style>"
        << "<h1>Vurus benchmark</h1><p class=\"note\">" << htmlString(protocol) << " · " << htmlString(r.startedUtc) << "</p><div class=\"score\">";
    if (auto score = r.score()) out << *score << " points"; else out << "Unscored run";
    out << "</div><p>" << (r.complete ? "Completed" : "Incomplete") << " · " << r.results.size() << '/' << scenes.size() << " scenes";
    if (!r.nonstandardReason.empty()) out << " · " << htmlString(r.nonstandardReason);
    if (!r.error.empty()) out << " · " << htmlString(r.error);
    out << "</p><p>1,000 points = 60 benchmark steps/second. Equal scene weighting using the geometric mean of each scene's mean throughput. Higher is faster.</p>"
        << "<div class=\"scroll\"><table><thead><tr><th>Map / start position</th><th>Benchmark FPS</th><th>Enemies nearby</th><th>RSS at start (MiB)</th><th>Sampled peak RSS (MiB)</th></tr></thead><tbody>";
    const auto memory = [&](std::optional<double> m) { if (m) out << *m; else out << "N/A"; };
    for (const auto& s : r.results) {
        out << "<tr><td>" << htmlString(label(s)) << "</td><td>" << 1000 / s.frame.mean << "</td><td>" << s.nearbyEnemies << "</td><td>";
        memory(s.startupRssMiB); out << "</td><td>"; memory(s.sampledPeakRssMiB); out << "</td></tr>";
    }
    out << "</tbody></table></div><p class=\"note\">Nearby = alive enemies within 1,000 world units at spawn, not all awake or on screen. RSS is process memory, not total GPU/system RAM; sampled peaks can miss shorter spikes.</p>"
        << "<div class=\"scroll\"><table><thead><tr><th>Map / start position</th><th>World</th><th>Animation</th><th>Audio</th><th>CPU total</th><th>Render / present</th><th>Frame</th></tr></thead><tbody>";
    for (const auto& s : r.results) out << "<tr><td>" << htmlString(label(s)) << "</td><td>" << s.world.mean << "</td><td>" << s.animation.mean
        << "</td><td>" << s.audio.mean << "</td><td>" << s.update.mean << "</td><td>" << s.render.mean << "</td><td>" << s.frame.mean << "</td></tr>";
    out << "</tbody></table></div><p class=\"note\">Mean milliseconds; CPU total includes World, animation and audio. One 1/60-second simulation tick plus one rendered/presented frame per step, uncapped. This is benchmark throughput, not ordinary catch-up gameplay FPS. Warmup "
        << r.warmup << " steps, then " << r.measured << " measured steps per scene. Load time, control changes, event polling, benchmark population/RSS sampling and report writing are outside measured frames. The progress/FPS overlay is included in rendering.</p>"
        << "<p><strong>" << (r.settingsPreset == "default" ? "Default benchmark preset" : "Game-menu video settings")
        << "</strong> · requested " << r.options.width << "×" << r.options.height
        << " · actual " << r.drawableWidth << "×" << r.drawableHeight
        << " · window mode " << r.options.windowMode << " · brightness " << r.options.gamma
        << " · HUD aspect " << r.options.hudAspect << " · "
        << (r.options.spriteFiltering == 0 ? "nearest" : r.options.spriteFiltering == 1 ? "bilinear" : "trilinear") << " sprites";
    const auto effect = [&](const char* name, bool value) { out << " · " << name << ' ' << (value ? "on" : "off"); };
    effect("shadows", r.options.shadows); effect("reactor", r.options.reactorFx);
    effect("vignette", r.options.vignette); effect("ground details", r.options.groundDetails);
    effect("CRT", r.options.crt); effect("CRT retro", r.options.crtRetro); effect("CRT bright", r.options.crtBright);
    effect("FXAA", r.options.fxaa); effect("SMAA", r.options.smaa);
    effect("bloom", r.options.bloom); effect("sharpen", r.options.sharpen);
    out << "</p><p>VSync off · uncapped · music off · Normal difficulty · invulnerable player; held fire except Burner crossfire";
    if (r.settingsSource == "game-defaults") out << " · no options.cfg found; game video defaults used";
    out << "</p><p>" << htmlString(r.platform) << " · " << htmlString(r.cpu) << " · " << htmlString(r.gpu) << "<br>" << htmlString(r.glVersion)
        << "<br>Audio: " << htmlString(r.audioBackend) << " · Build: " << htmlString(r.build) << "</p>"
        << "<p><a href=\"report.json\">Full JSON report: medians, p95, p99, maxima and populations</a></p>"
        << "<small>Compare scores only at matching effective video settings, actual resolution, protocol and map fingerprints. Game-menu video settings are read only; no settings, saves or leaderboard files are written. No external fonts, scripts or network access are needed to view this report.</small></html>";
    return out.str();
}
} // namespace vyruz::benchmark
