// Shared benchmark runner for the game menu and standalone entry point.
#include "benchmark/report.h"
#include "render/commander_ui.h"
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifndef VURUS_BENCH_BUILD
#define VURUS_BENCH_BUILD "unversioned"
#endif
namespace {
namespace bench = vyruz::benchmark;
using Clock = std::chrono::steady_clock;
bool benchQuitRequested=false;
void drawBenchmarkProgress(App& a) {
    if (benchProgress.empty()) return;
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0, 0, 0, .8f); glBegin(GL_QUADS);
    glVertex2f(8, 445); glVertex2f(632, 445); glVertex2f(632, 478); glVertex2f(8, 478); glEnd();
    glEnable(GL_TEXTURE_2D); glColor4f(1, 1, 1, 1);
    a.hud.drawMenuText(a.gfx, benchProgress, 18, 467, 4, 605);
    glPopAttrib(); glPopMatrix();
}
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
double milliseconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
std::string timeText(bool filename) {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out; out << std::put_time(&utc, filename ? "%Y%m%d-%H%M%S" : "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::filesystem::path newRunDirectory(const std::filesystem::path& parent) {
    std::filesystem::create_directories(parent);
    const auto stem = timeText(true);
    for (int n = 0; n < 10000; ++n) {
        const auto candidate = parent / (stem + (n ? "-" + std::to_string(n) : ""));
        if (std::filesystem::create_directory(candidate)) return candidate;
    }
    throw std::runtime_error("Cannot create a unique benchmark result directory");
}
// Resolve existing resources only. Import/recovery, saves and options never run.
std::string existingResources(const std::filesystem::path& input) {
    const auto valid = [](const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) return false;
        bool candidate = false;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            const auto name = levelKey(entry.path().filename().string());
            if (name == "VZ-DATA.RSC" || name == "COMMON" || name == "UNDERGROUND") candidate = true;
        }
        if (!candidate) return false;
        vyruz::Vfs vfs; return vfs.init(path.string());
    };
    if (valid(input)) return input.string();
    for (const auto& root : {input / "data", input}) {
        std::string key; std::ifstream receipt(root / "current.txt"); std::getline(receipt, key);
        if (!key.empty() && key.find_first_not_of("0123456789abcdef") == std::string::npos && valid(root / key))
            return (root / key).string();
        if (valid(root)) return root.string();
    }
    throw std::runtime_error("No existing game data found. Run Vurus to import your disc first, or use --data RESOURCE_DIRECTORY.");
}
struct Session {
    App app;
    ~Session() {
        app.audio.device().close();
        if (app.window && SDL_GL_GetCurrentContext()) {
            app.loading.clear(app.gfx); app.sceneRenderer.clear(app.gfx); app.hud.clear(app.gfx);
            app.gfx.deleteTexture(app.atlasTex); app.gfx.shutdown();
        }
        app.controller.close();
        if (app.window) SDL_DestroyWindow(app.window);
    }
};
void nonstandard(bench::Report& report, const std::string& reason) {
    report.standard = false;
    if (report.nonstandardReason.find(reason) != std::string::npos) return;
    if (!report.nonstandardReason.empty()) report.nonstandardReason += "; ";
    report.nonstandardReason += reason;
}
std::string cpuDescription() {
#if defined(__linux__)
    std::ifstream model("/proc/device-tree/model", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(model)), {});
    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
    if (!text.empty()) return text;
    std::ifstream cpu("/proc/cpuinfo");
    while (std::getline(cpu, text)) {
        if (text.starts_with("model name") || text.starts_with("Hardware")) {
            const auto colon = text.find(':'); if (colon != std::string::npos) return text.substr(colon + 1);
        }
    }
#endif
    return std::to_string(SDL_GetCPUCount()) + " logical CPUs";
}
std::string glText(GLenum name) {
    const auto* value = glGetString(name); return value ? reinterpret_cast<const char*>(value) : "unavailable";
}
std::optional<double> rss() {
    const auto memory = vyruz::processMemory(false);
    return memory.rss ? std::optional<double>(double(*memory.rss) / 1024) : std::nullopt;
}
void sampleMemory(bench::Result& result) {
    if (auto value = rss()) result.sampledPeakRssMiB = std::max(result.sampledPeakRssMiB.value_or(0), *value);
}
bool cancelRequested(App& a, bench::Report& report) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        a.controller.event(event);
        if (event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(a.window) &&
            (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED)) {
            int width = 0, height = 0; SDL_GetWindowSize(a.window, &width, &height);
            if (width > 0 && height > 0) a.gfx.resize(width, height);
            SDL_GL_GetDrawableSize(a.window, &width, &height);
            if (width != report.drawableWidth || height != report.drawableHeight)
                nonstandard(report, "drawable size changed during benchmark");
        }
        if (event.type == SDL_QUIT) { benchQuitRequested=true; return true; }
        if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == SDLK_ESCAPE) return true;
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B) return true;
    }
    return false;
}
std::string fingerprint(App& a, const char* path) {
    std::vector<uint8_t> data; require(a.vfs.read(path, data), "Cannot fingerprint benchmark map");
    uint64_t hash = 14695981039346656037ULL;
    for (auto value : data) { hash ^= value; hash *= 1099511628211ULL; }
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << hash; return out.str();
}
void initialize(Session& session, const std::string& data, bench::Report& report) {
    auto& a = session.app;
#if VYRUZ_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    a.options = report.options; a.retryPersistence = false;
    a.window = SDL_CreateWindow("Vurus benchmark", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        a.options.width, a.options.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    require(a.window && a.gfx.init(a.window), std::string("Graphics initialization failed: ") + SDL_GetError());
    SDL_ShowCursor(SDL_DISABLE);
    require(a.vfs.init(data), a.vfs.error());
    std::string error;
    if (!a.audio.device().configure({48000, 32, 2}, error) || !a.audio.device().available()) nonstandard(report, "audio unavailable");
    a.audio.device().setMasterVolume(.7f);
    const char* driver = SDL_GetCurrentAudioDriver(); report.audioBackend = driver ? driver : "unavailable";
    if (!driver || !std::strcmp(driver, "dummy") || !std::strcmp(driver, "disk")) nonstandard(report, "test or unavailable audio backend");
    bool displayFailure = false;
    bool applied = applyOptionsRuntime(a, a.options, error, true, &displayFailure);
    if (!applied && displayFailure) {
        int width = 0, height = 0; SDL_GetWindowSize(a.window, &width, &height);
        if (bench::useExistingWindowSize(a.options, width, height)) {
            std::fprintf(stderr, "benchmark: requested window size unavailable; using %dx%d for this run\n", width, height);
            applied = applyOptionsRuntime(a, a.options, error, false);
        }
    }
    require(applied, "Cannot apply benchmark options: " + error);
    // Report the effective geometry, including native fixed-display fallback.
    // The preset marker and the default 640x480 comparability check remain intact.
    report.options.width = a.options.width; report.options.height = a.options.height;
    report.swapInterval = SDL_GL_GetSwapInterval();
    SDL_GL_GetDrawableSize(a.window, &report.drawableWidth, &report.drawableHeight);
    if (a.gfx.vsyncEnabled() || report.swapInterval != 0) nonstandard(report, "driver VSync enabled");
    if (report.settingsPreset == "default" && (report.drawableWidth != 640 || report.drawableHeight != 480))
        nonstandard(report, "default preset drawable is not 640x480");
    report.gpu = glText(GL_RENDERER); report.glVersion = glText(GL_VERSION);
    a.controller.scan();
}
bool runScene(Session& session, const bench::Scene& spec, size_t index, bench::Report& report) {
    auto& a = session.app; benchProgress = "LOADING " + std::to_string(index + 1) + "/" + std::to_string(bench::scenes.size()) + " " + spec.map;
    require(loadLevelData(a, spec.map, nullptr, false, false, false, 1), a.loadError);
    a.presentation.close(); a.menu.close(a.paused); a.paused = false; a.sessionStarted = true;
    a.world.setDebugGodMode(true); a.world.debugFillAmmo();
    std::vector<vyruz::ContentPosition> stations;
    for (size_t i = 0; i < a.world.actors().size(); ++i)
        if (!a.world.actors()[i].generators.empty()) stations.push_back(a.content.objects[i].position);
    if (spec.generatorTour) {
        require(stations.size() == 35, "Generator workload requires the original 35 stations");
        a.world.setPlayerPosition(stations[0].x + 100, stations[0].y + 50);
    } else a.world.setPlayerPosition(spec.x, spec.y);
    if (spec.burnerCrossfire) {
        // Keep the authored attackers alive for sustained flame load. This is
        // benchmark setup only; their normal AI, weapons and cadence are used.
        auto snapshot = a.world.snapshot();
        for (auto& actor : snapshot.actors) if (actor.enemy) actor.indestructible = true;
        std::string error; require(a.world.restoreSnapshot(snapshot, error), error);
        a.world.setDebugGodMode(true); a.world.setPlayerDirection(0);
    }
    bench::Result result; result.id = spec.id; result.map = spec.map;
    result.burnerCrossfire = spec.burnerCrossfire;
    result.mapFingerprint = fingerprint(a, spec.map);
    const auto startPosition = a.world.playerPosition(); result.x = startPosition.x; result.y = startPosition.y;
    result.initialRows = a.world.actors().size(); result.startupRssMiB = rss(); result.sampledPeakRssMiB = result.startupRssMiB;
    for (size_t i = 0; i < a.world.actors().size(); ++i) {
        const auto& actor = a.world.actors()[i]; const auto p = a.content.objects[i].position;
        const double dx = p.x - result.x, dy = p.y - result.y;
        if (actor.alive && actor.enemy && dx * dx + dy * dy <= 1000000) ++result.nearbyEnemies;
    }
    a.audio.device().setPaused(false);
    std::array<std::vector<double>, 6> samples;
    for (auto& sample : samples) sample.reserve(report.measured);
    const int steps = report.warmup + report.measured;
    for (int tick = 0; tick < steps; ++tick) {
        if (cancelRequested(a, report)) return false;
        if (tick % 600 == 0) {
            a.world.debugFillAmmo();
            if (spec.generatorTour) {
                const auto p = stations[(tick / 600) % stations.size()]; a.world.setPlayerPosition(p.x + 100, p.y + 50);
            }
        }
        if (!spec.burnerCrossfire) a.world.setPlayerDirection((tick / 120) % 24);
        vyruz::WorldInput input; input.fire = !spec.burnerCrossfire;
        input.weaponDelta = !spec.burnerCrossfire && tick > 0 && tick % 600 == 0 ? 1 : 0;
        benchProgress = std::to_string(index + 1) + "/" + std::to_string(bench::scenes.size()) + " " + spec.map + (tick < report.warmup ? " WARMUP " : " TEST ") + std::to_string(100 * tick / steps) + "%  B: CANCEL";
        const auto begin = Clock::now();
        a.previousPositions.clear(); for (const auto& object : a.content.objects) a.previousPositions.push_back(object.position);
        bindEffectCamera(a);
        const auto worldStart = Clock::now(); a.world.tick(1.f / 60, input); const auto worldEnd = Clock::now();
        const auto p = a.world.playerPosition(); a.worldX = p.x; a.worldY = p.y; a.motion.direction = a.world.player().direction;
        const auto animationStart = Clock::now();
        advanceSceneAnimation(a, 1000.0 / 60, a.world.elapsedMilliseconds()); a.hud.advance(1000.0 / 60, &a.world);
        const auto animationEnd = Clock::now();
        a.audio.setListenerHalfWidth(320); consumeGameplayAudio(a);
        a.audio.updateSpeech(); a.audio.updateMusic(); const auto audioEnd = Clock::now();
        for (auto [id, revision] : a.sceneRenderer.finishedGates(a.world.actors())) a.world.finishGateAnimation(id, revision);
        for (auto id : a.sceneRenderer.finishedEffects(a.world.actors())) a.world.expireVisual(id);
        const auto renderStart = Clock::now(); render(a, 1.f); const auto end = Clock::now();
        require(!a.renderFailed, "Scene animation or rendering failed"); recordDebugFrame(a, milliseconds(begin, end));
        if (tick >= report.warmup) {
            const std::array<double, 6> values{milliseconds(worldStart, worldEnd), milliseconds(animationStart, animationEnd),
                milliseconds(animationEnd, audioEnd), milliseconds(begin, renderStart), milliseconds(renderStart, end), milliseconds(begin, end)};
            for (size_t i = 0; i < values.size(); ++i) samples[i].push_back(values[i]);
            result.maxProjectiles = std::max(result.maxProjectiles, a.world.projectiles().size());
            if (spec.burnerCrossfire) {
                size_t firing = 0;
                for (const auto& actor : a.world.actors()) if (actor.alive && actor.nativeSubtype == 0x1a)
                    for (const auto& weapon : actor.weapons)
                        if (weapon.state.lastFireMs && a.world.elapsedMilliseconds() >= *weapon.state.lastFireMs &&
                            a.world.elapsedMilliseconds() - *weapon.state.lastFireMs < 100) { ++firing; break; }
                result.maxBurnersFiring = std::max(result.maxBurnersFiring, firing);
                if (firing >= 2) ++result.burnerCrossfireSteps;
            }
        }
        if (tick % 120 == 0) sampleMemory(result);
    }
    sampleMemory(result); result.finalRows = a.world.actors().size();
    for (const auto& actor : a.world.actors()) if (actor.alive) ++result.finalLive;
    result.world = bench::summarize(std::move(samples[0])); result.animation = bench::summarize(std::move(samples[1]));
    result.audio = bench::summarize(std::move(samples[2])); result.update = bench::summarize(std::move(samples[3]));
    result.render = bench::summarize(std::move(samples[4])); result.frame = bench::summarize(std::move(samples[5]));
    if (spec.burnerCrossfire && !result.burnerCrossfireSteps)
        nonstandard(report, "Burner encounter produced no overlapping fire");
    std::printf("BENCH scene=%s fps=%.2f world=%.3f render=%.3f rows=%zu live=%zu\n", spec.id, 1000 / result.frame.mean,
        result.world.mean, result.render.mean, result.finalRows, result.finalLive); std::fflush(stdout);
    report.results.push_back(std::move(result)); return true;
}
void writeReports(const std::filesystem::path& out, const bench::Report& report) {
    for (const auto& [name, contents] : std::array<std::pair<const char*, std::string>, 2>{{
        {"report.json", bench::jsonReport(report)}, {"report.html", bench::htmlReport(report)}}}) {
        std::ofstream file(out / name, std::ios::binary | std::ios::trunc);
        require(bool(file), "Cannot open benchmark report: " + (out / name).string());
        file << contents; file.close(); require(bool(file), "Cannot finish benchmark report: " + (out / name).string());
    }
    std::cout << "REPORT_DIRECTORY " << out.string() << '\n';
}
void resultsScreen(App& a, const bench::Report& report, const std::filesystem::path& out, bool returnToMenu,
                   std::string_view destination="MAIN MENU") {
    benchProgress.clear(); a.audio.device().setPaused(true);
    a.gfx.beginFrame(640,480);a.gfx.clear(.035f,.06f,.055f);
    const auto view=a.gfx.viewport();glTranslatef(view.uiX(),view.uiY(),0);
    const vyruz::CommanderUi ui{a.gfx,a.hud};
    ui.text("VURUS / BENCHMARK RESULTS",20,33,600,1.3f);
    ui.box(12,61,628,143);ui.frame(12,61,628,143);
    const auto score=report.score();
    ui.text(score?std::to_string(*score)+" POINTS":"UNSCORED RUN",26,94,350,1.5f);
    ui.text(std::string(report.settingsPreset=="default"?"DEFAULT":"USER")+" / "+
        std::to_string(report.drawableWidth)+" X "+std::to_string(report.drawableHeight),400,87,210,1.f);
    ui.text(std::to_string(report.results.size())+" / 7 SCENES",400,113,210,1.f);
    ui.text(!report.complete?"INCOMPLETE":score?"COMPLETE / 1000 POINTS = 60 STEPS/S":"COMPLETE / CUSTOM OR UNSCORED",26,124,565,1.f);
    ui.box(12,155,628,381);ui.frame(12,155,628,381);
    ui.box(20,163,620,190,.12f,.17f,.12f);
    ui.text("SCENE",26,182,250,1.f);ui.text("FPS",295,182,72,1.f);
    ui.text("WORLD MS",385,182,100,1.f);ui.text("RAM MIB",515,182,100,1.f);
    float y=213;
    for(size_t i=0;i<report.results.size();++i) {
        const auto& result=report.results[i];
        if(i%2==0)ui.box(20,y-18,620,y+6,.06f,.10f,.08f);
        std::string name=result.id;std::replace(name.begin(),name.end(),'-',' ');
        ui.text(name,26,y,250,1.05f);
        const auto number=[](double value){char text[40];std::snprintf(text,sizeof(text),"%.1f",value);return std::string(text);};
        ui.text(result.frame.mean>0?number(1000/result.frame.mean):"N/A",295,y,72,1.05f);
        ui.text(number(result.world.mean),385,y,100,1.05f);
        ui.text(result.sampledPeakRssMiB?number(*result.sampledPeakRssMiB):"N/A",515,y,100,1.05f);y+=25;
    }
    std::string status=!report.error.empty()?report.error:!report.nonstandardReason.empty()?report.nonstandardReason:"FIXED-STEP THROUGHPUT / HTML + JSON REPORTS SAVED";
    if(status.size()>80){status.resize(76);const auto space=status.rfind(' ');if(space>55&&space!=std::string::npos)status.resize(space);status+="...";}
    ui.text(status,20,405,600,1.f);
    std::string path=out.parent_path().filename().string()+"/"+out.filename().string();
    if(path.size()>72)path="..."+path.substr(path.size()-69);
    ui.text(path,20,430,600,1.f);
    ui.box(12,447,628,477,.12f,.17f,.12f);
    ui.text(std::string("ENTER / ESC / A / B: ")+(returnToMenu?std::string(destination):"EXIT"),22,468,590,1.f);
    a.gfx.present();
}
bool holdResults(App& a,const bench::Report& report,const std::filesystem::path& out,bool returnToMenu,
                 std::string_view destination="MAIN MENU") {
    SDL_FlushEvent(SDL_KEYDOWN);SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
    SDL_Event event;
    while(SDL_WaitEvent(&event)) {
        if(event.type==SDL_QUIT)return true;
        if(event.type==SDL_WINDOWEVENT&&(event.window.event==SDL_WINDOWEVENT_EXPOSED||
           event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||event.window.event==SDL_WINDOWEVENT_RESIZED)) {
            int width=0,height=0;SDL_GetWindowSize(a.window,&width,&height);
            if(width>0&&height>0)a.gfx.resize(width,height);
            resultsScreen(a,report,out,returnToMenu,destination);
        }
        if(event.type==SDL_KEYDOWN&&!event.key.repeat&&(event.key.keysym.sym==SDLK_RETURN||event.key.keysym.sym==SDLK_ESCAPE))return false;
        if(event.type==SDL_CONTROLLERBUTTONDOWN&&(event.cbutton.button==SDL_CONTROLLER_BUTTON_A||
           event.cbutton.button==SDL_CONTROLLER_BUTTON_B||event.cbutton.button==SDL_CONTROLLER_BUTTON_START))return false;
        a.controller.event(event);
    }
    return true;
}
} // namespace

namespace {
void showLastBenchmark(App& a) {
    if(!a.lastBenchmark)return;
    const auto& saved=*a.lastBenchmark;
    resultsScreen(a,saved.report,saved.directory,true,"BENCHMARK MENU");
    if(holdResults(a,saved.report,saved.directory,true,"BENCHMARK MENU"))a.quit=1;
    a.audio.device().setPaused(a.paused);
}
}

int runBenchmark(int argc, char** argv, bool returnToMenu=false) {
    benchQuitRequested=false;
    SDL_SetMainReady();
    std::string data = binaryDirectory();
    for (const char* key : {"VYRUZ_DATA_DIR", "VURUS_DATA_DIR"}) if (const char* value = std::getenv(key); value && *value) data = value;
    std::filesystem::path output = std::filesystem::path(binaryDirectory()) / "benchmark-results";
    std::string optionsPath = defaultOptionsPath(), controllerMap;
    int measured = bench::measuredSteps; bool wait = true, defaultSettings = false; std::string shot;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help") {
            std::puts("Vurus benchmark - separate scripted seven-scene performance test\nUsage: vurus-bench [--data RESOURCE_DIRECTORY] [--default-settings] [--options-file FILE] [--output DIRECTORY] [--no-wait] [--frames N] [--shot RESULT.ppm]\nDefault: read video settings saved by the game menu in options.cfg beside this binary.\nAlways uncapped, VSync off; fixed benchmark audio/difficulty and scripted workload.\n--default-settings ignores menu settings: original 640x480 nearest benchmark preset.\n--options-file reads another game's options.cfg (video fields only; never writes it).\n120 warmup + 1200 measured steps/scene; 1000 points = 60 complete benchmark steps/s.\nCompare scores only at matching effective video settings/output resolution.\n--frames changes the workload and produces an unscored test run (1..36000).\nResults: benchmark-results/RUN/report.html and report.json beside this binary.\nUses existing imported data; never reads/writes game saves or leaderboard.\nEsc or controller B cancels; results remain on screen until a button is pressed."); return 0;
        }
        if (argument == "--licenses") { BenchmarkLaunch unused; return runApplication(argc, argv, unused, false); }
        if ((argument == "--data" || argument == "--output" || argument == "--frames" || argument == "--shot" || argument == "--options-file" || argument == "--controller-map") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (argument == "--data") data = value;
            else if (argument == "--output") output = value;
            else if (argument == "--shot") shot = value;
            else if (argument == "--options-file") optionsPath = value;
            else if (argument == "--controller-map") controllerMap = value;
            else {
                char* end = nullptr; const long n = std::strtol(value.c_str(), &end, 10);
                if (end == value.c_str() || *end || n < 1 || n > 36000) { std::fputs("--frames must be 1..36000\n", stderr); return 1; }
                measured = int(n);
            }
        } else if (argument == "--no-wait") wait = false;
        else if (argument == "--default-settings") defaultSettings = true;
        else { std::fprintf(stderr, "Unknown option or missing value: %s; use --help\n", argv[i]); return 1; }
    }
    bench::Report report; report.startedUtc = timeText(false); report.build = VURUS_BENCH_BUILD;
    report.measured = measured;
    if (measured != bench::measuredSteps) nonstandard(report, "custom sample count");
    std::filesystem::path runDirectory; std::unique_ptr<Session> session; int exitCode = 0;
    try {
        data = existingResources(data); runDirectory = newRunDirectory(output);
        vyruz::GameOptions menu;
        if (!defaultSettings) {
            std::string error;
            if (!vyruz::loadOptions(optionsPath, menu, error))
                throw std::runtime_error("Cannot read menu settings: " + error);
            report.settingsPreset = "user";
            report.settingsSource = std::filesystem::exists(optionsPath) ? "game-menu" : "game-defaults";
        }
        report.options = bench::resolveOptions(defaultSettings ? nullptr : &menu, VYRUZ_GLES);
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0, std::string("SDL initialization failed: ") + SDL_GetError());
        if (!controllerMap.empty()) require(SDL_GameControllerAddMappingsFromFile(controllerMap.c_str())>=0, "Cannot read controller mappings: " + std::string(SDL_GetError()));
        report.platform = SDL_GetPlatform(); report.cpu = cpuDescription();
        for (size_t i = 0; i < bench::scenes.size(); ++i) {
            session.reset(); session = std::make_unique<Session>(); initialize(*session, data, report);
            if (!runScene(*session, bench::scenes[i], i, report)) { report.error = "Cancelled"; exitCode = 2; break; }
        }
        report.complete = report.results.size() == bench::scenes.size();
    } catch (const std::exception& error) { report.error = error.what(); exitCode = 1; std::fprintf(stderr, "BENCH_ERROR %s\n", error.what()); }
    try {
        if (!runDirectory.empty()) writeReports(runDirectory, report);
        if (report.score()) std::printf("VURUS_BENCH_SCORE %u\n", *report.score()); else std::puts("VURUS_BENCH_UNSCORED");
        if (session && session->app.haveLevel && session->app.window && SDL_GL_GetCurrentContext()) {
            auto& app = session->app;
            if (!shot.empty()) app.gfx.setSwapEnabled(false);
            resultsScreen(app, report, runDirectory, returnToMenu);
            if (!shot.empty()) {
                glFinish();
                require(saveScreenshot(shot.c_str(), app.gfx.drawableWidth(), app.gfx.drawableHeight()), "Cannot capture benchmark results");
                app.gfx.setSwapEnabled(true); SDL_GL_SwapWindow(app.window);
            }
            if(wait&&!benchQuitRequested&&holdResults(app,report,runDirectory,returnToMenu))benchQuitRequested=true;
        } else if (!report.error.empty() && wait) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Vurus benchmark", report.error.c_str(), nullptr);
    } catch (const std::exception& error) { std::fprintf(stderr, "BENCH_ERROR %s\n", error.what()); exitCode = 1; }
    session.reset(); benchProgress.clear(); SDL_Quit();
    return returnToMenu && benchQuitRequested ? 3 : exitCode;
}
