#pragma once
#include <array>
#include <cstddef>
#include <string>

namespace vyruz {
enum class OptionAction : size_t {
    Forward, Reverse, Left, Right, Fire, NextWeapon, PrevWeapon, Map, Inventory, Pause, Count
};
constexpr size_t optionActionCount = static_cast<size_t>(OptionAction::Count);
const char* optionActionName(OptionAction action);

// Port preferences, independent of a saved game. SDL scancodes are physical keys.
// Defaults retain the accepted input behavior and existing output configuration.
struct GameOptions {
#if VYRUZ_PORTMASTER
    int width = 640, height = 480;
#else
    int width = 1280, height = 960;
#endif
    int windowMode = 0; // Windowed, exclusive Fullscreen, Borderless fullscreen, Borderless window.
    int gamma = 50; // Bounded port brightness: 0..100, neutral50; legacy key retained.
    int spriteFiltering = 0; // Off, Bilinear, Trilinear (port presentation option).
    bool fxaa=false,bloom=false,sharpen=false; // Optional scene-only filters.
    bool smaa = false;
    bool fpsCounter = false; // Optional presentation overlay; does not enable debug mode.
    bool crtBright = false; // Warm CRT with highlight glow.
    bool crtRetro = false; // Warm variant, used only while CRT is enabled.
    bool crt = false; // Optional adapted MAME-PSGS postprocess; unchanged path when Off.
    bool vignette = true; // Original V-PANEL TV Borders, optional Linux presentation.
    int hudLanguage = 1; // Native text/marker language1 English,2 French.
    int hudAspect = 0; // Auto follows actual window; manual ratios only affect HUD.
    bool shadows = true, reactorFx = true, groundDetails = true;
    bool soundFx = true, voices = true, music = true;
    bool menuSounds = true; // Menu hover/selection cues; independent of game FX.
    float effectsVolume = 1.f, voicesVolume = 1.f, musicVolume = 1.f;
    int audioBits = 32, audioRate = 48000, audioChannels = 2;
    std::string playerName = "Player";
    // Stable save directory identity; empty preserves legacy name-based storage.
    std::string saveProfile;
    const std::string& storageProfile() const { return saveProfile.empty()?playerName:saveProfile; }
    int difficulty = 1; // Easy, Normal, Hard, Insane.
    std::array<int, optionActionCount> keyBindings = {26,22,4,7,44,8,20,16,12,19};
    // -1 leaves that action unbound; analog sticks and trigger keep their roles.
    std::array<int, optionActionCount> controllerBindings = {11,12,13,14,0,10,9,4,3,2};
    int fireButton = 1, thrustButton = 3; // SDL mouse buttons; 0 means unbound.
    int joystickMoveSensitivity = 100, joystickAimSensitivity = 100;
    bool crosshair = true;
    int aimAssist = 0; // Off, Slight, Medium, Full, Retaliate; controller only.
    int joystickDeadzone = 22; // Percent; defaults retain the accepted controller response.
    bool operator==(const GameOptions&) const = default;
};
bool validateOptions(const GameOptions& options, std::string& error);
// Missing files succeed without changing options; invalid files fail atomically.
// Known keys are strict; unknown keys are ignored for forward compatibility.
bool loadOptions(const std::string& path, GameOptions& options, std::string& error);
// Caller supplies an existing parent directory. Same-directory atomic replacement.
bool saveOptions(const std::string& path, const GameOptions& options, std::string& error);
} // namespace vyruz
