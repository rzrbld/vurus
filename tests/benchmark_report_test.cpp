#include "benchmark/report.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <locale>
using namespace vyruz::benchmark;
int main() {
    vyruz::GameOptions menu;
    menu.width = 1920; menu.height = 1080; menu.windowMode = 2;
    menu.gamma = 73; menu.hudAspect = 2; menu.spriteFiltering = 2;
    menu.shadows = menu.reactorFx = menu.vignette = false; menu.groundDetails = true;
    menu.crt = menu.crtRetro = menu.crtBright = menu.smaa = menu.bloom = menu.sharpen = true;
    menu.vsync = true; menu.frameCap = 30; menu.music = true; menu.soundFx = menu.voices = false;
    menu.audioRate = 11025; menu.difficulty = 3; menu.playerName = "User"; menu.saveProfile = "private";
    const auto snapshot = menu, user = resolveOptions(&menu, false), fixed = resolveOptions(nullptr, false);
    assert(menu == snapshot); // Never mutate the menu's source object.
    assert(user.width == 1920 && user.height == 1080 && user.windowMode == 2);
    assert(user.gamma == 73 && user.hudAspect == 2 && user.spriteFiltering == 2);
    assert(!user.shadows && !user.reactorFx && !user.vignette && user.groundDetails);
    assert(user.crt && user.crtRetro && user.crtBright && user.smaa && user.bloom && user.sharpen);
    assert(!user.vsync && user.frameCap == 0 && !user.music && user.soundFx && user.voices);
    assert(user.audioRate == 48000 && user.difficulty == 1 && user.playerName == "Benchmark" && user.saveProfile.empty());
    assert(fixed.width == 640 && fixed.height == 480 && fixed.windowMode == 0 && fixed.spriteFiltering == 0);
    assert(fixed.shadows && fixed.reactorFx && fixed.vignette && !fixed.groundDetails && !fixed.crt);
    assert(resolveOptions(&menu, false).fxaa == menu.fxaa);
    const auto gles = resolveOptions(&menu, true);
    assert(gles.width == 1920 && gles.groundDetails && !gles.crt && !gles.crtRetro && !gles.crtBright);
    assert(!gles.fxaa && !gles.smaa && !gles.bloom && !gles.sharpen);
    auto native = resolveOptions(&menu, true);
    native.windowMode = 0;
    const auto nativeBefore = native;
    assert(useExistingWindowSize(native, 480, 320));
    assert(native.width == 480 && native.height == 320);
    auto geometryOnly = nativeBefore; geometryOnly.width = 480; geometryOnly.height = 320;
    assert(native == geometryOnly && menu == snapshot);
    assert(!useExistingWindowSize(native, 480, 320));
    assert(!useExistingWindowSize(native, 0, 320));
    assert(!useExistingWindowSize(native, 480, -1));
    for (int mode : {1, 2}) {
        native.windowMode = mode; const auto unchanged = native;
        assert(!useExistingWindowSize(native, 640, 480) && native == unchanged);
    }
    native.windowMode = 3; assert(useExistingWindowSize(native, 640, 480));
    std::vector<double> samples;
    for (int n = 100; n >= 1; --n) samples.push_back(n);
    const auto stats = summarize(samples);
    assert(stats.count == 100 && stats.mean == 50.5 && stats.p50 == 51);
    assert(stats.p95 == 96 && stats.p99 == 100 && stats.maximum == 100);
    assert(summarize({3}).p99 == 3 && summarize({}).count == 0);
    for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        assert(!calculateScore({bad})); bool rejected = false;
        try { summarize({1, bad}); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    assert(!calculateScore({}));
    assert(calculateScore({1000.0 / 60}) == 1000);
    assert(calculateScore({1000.0 / 120, 1000.0 / 120}) == 2000);
    assert(calculateScore({10, 40}) == 833);
    assert(calculateScore({10, 40}) == calculateScore({10, 10, 40, 40}));
    Report report; report.complete = true;
    for (const auto& scene : scenes) {
        Result result; result.id = scene.id; result.map = scene.map;
        result.burnerCrossfire = scene.burnerCrossfire;
        if (scene.burnerCrossfire) { result.maxBurnersFiring = 3; result.burnerCrossfireSteps = 1100; }
        result.frame = summarize(std::vector<double>(measuredSteps, 1000.0 / 60));
        report.results.push_back(result);
    }
    assert(report.score() == 1000);
    // A score must include the expanded workload and actual overlapping fire.
    report.results.back().burnerCrossfireSteps = 0; assert(!report.score());
    report.results.back().burnerCrossfireSteps = 1100;
    auto last = report.results.back(); report.results.pop_back(); assert(!report.score());
    report.results.push_back(last);
    report.settingsPreset = "user"; report.settingsSource = "game-menu"; report.options = user;
    assert(report.score() == 1000); // User resolution/effects runs still get a score.
    const auto customJson = jsonReport(report), customHtml = htmlReport(report);
    assert(customJson.find("\"width\":1920,\"height\":1080") != std::string::npos);
    assert(customJson.find("\"filtering\":2,\"shadows\":false") != std::string::npos);
    assert(customJson.find("\"smaa\":true,\"bloom\":true,\"sharpen\":true") != std::string::npos);
    assert(customJson.find("\"settings_preset\":\"user\"") != std::string::npos);
    assert(customHtml.find("Game-menu video settings") != std::string::npos);
    assert(customHtml.find("7/7 scenes") != std::string::npos);
    assert(customJson.find("\"protocol\":\"vurus-bench-2\"") != std::string::npos);
    assert(customJson.find("\"schema_version\":3") != std::string::npos);
    assert(customJson.find("\"steps_with_two_burners_firing\":1100") != std::string::npos);
    assert(customHtml.find("1920×1080") != std::string::npos && customHtml.find("SMAA on") != std::string::npos);
    report.complete = false; assert(!report.score()); report.complete = true;
    report.standard = false; assert(!report.score()); report.standard = true;
    report.results.back().frame.count--; assert(!report.score()); report.results.back().frame.count++;
    report.results.back().id = "different workload"; assert(!report.score()); report.results.back().id = scenes.back().id;
    report.gpu = "<script>alert(\"x\")</script>&";
    report.error = "line\nquote\"\\\t";
    report.platform = std::string("control") + char(1);
    const auto html = htmlReport(report), json = jsonReport(report);
    assert(html.find("<script>") == std::string::npos);
    assert(html.find("&lt;script&gt;") != std::string::npos);
    assert(json.find("\\u0001") != std::string::npos);
    assert(json.find("line\\nquote\\\"\\\\\\t") != std::string::npos);
    assert(json.find("\"score\":1000") != std::string::npos);
    assert(html.find("https://") == std::string::npos && html.find("http://") == std::string::npos);
    Report reopened;std::string error;
    assert(readReport(json,reopened,error));
    struct CommaDecimal:std::numpunct<char>{char do_decimal_point()const override{return ',';}};
    const auto savedLocale=std::locale();
    std::locale::global(std::locale(std::locale::classic(),new CommaDecimal));
    assert(readReport(json,reopened,error));
    std::locale::global(savedLocale);
    assert(reopened.score()==report.score()&&reopened.results.size()==7&&reopened.gpu==report.gpu&&reopened.error==report.error);
    assert(std::abs(reopened.results.back().frame.mean-report.results.back().frame.mean)<1e-7&&reopened.options.width==1920);
    const auto intact=jsonReport(reopened);
    for(const auto& invalid:{json.substr(0,json.size()-3),json+"false",std::string("{\"schema_version\":3,\"schema_version\":3}"),std::string(2*1024*1024+1,' ')}){
        assert(!readReport(invalid,reopened,error)&&jsonReport(reopened)==intact);
    }
    auto invalid=json;invalid.replace(invalid.find("vurus-bench-2"),13,"vurus-bench-9");assert(!readReport(invalid,reopened,error));
    report.complete=false;report.results.resize(2);report.standard=false;report.error="Cancelled";
    assert(readReport(jsonReport(report),reopened,error)&&!reopened.complete&&!reopened.score()&&reopened.results.size()==2);
    const auto dir=std::filesystem::temp_directory_path()/("vurus-last-bench-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir/"older");std::filesystem::create_directories(dir/"newer");
    assert(!latestReport(dir));
    {std::ofstream f(dir/"older/report.json");f<<json;}
    {std::ofstream f(dir/"newer/report.json");f<<"{broken";}
    std::filesystem::last_write_time(dir/"older/report.json",std::filesystem::file_time_type::clock::now()-std::chrono::seconds(10));
    assert(latestReport(dir)->directory.filename()=="older");
    {std::ofstream f(dir/"newer/report.json");f<<jsonReport(report);}
    assert(latestReport(dir)->directory.filename()=="newer"&&!latestReport(dir)->report.complete);
    std::filesystem::remove_all(dir);
}
