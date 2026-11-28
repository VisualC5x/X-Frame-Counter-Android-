#pragma once
#include <Geode/Geode.hpp>
#include "Analyzer.hpp"
#include "GDR.hpp"
#include <filesystem>
#include <cstring>
#include <string_view>
#include <functional>

using namespace geode::prelude;

namespace xfc {

inline constexpr float kStep = 1.f / 240.f;

// Theme: blue / black
inline constexpr ccColor3B kAccent{0, 170, 255};
inline constexpr ccColor3B kAccent2{60, 230, 255};
inline constexpr ccColor3B kPanelBg{5, 9, 20};

inline const char* font() { return "bigFont.fnt"; }

// Settings with built-in defaults: if a key is ever missing (old mod.json, failed parse) the mod still works and is visible.
namespace detail {
struct SetDef { const char* k; double v; };
inline constexpr SetDef kSetDefs[] = {
    {"show-hud", 1}, {"show-tiers", 1}, {"show-ms", 1}, {"hud-scale", 1.0}, {"show-rings", 1}, {"show-release-rings", 1}, {"ring-label", 1},
    {"ring-scale", 1.3}, {"ring-time", 1.5}, {"tps-enabled", 0}, {"tps", 240}, {"fps-enabled", 0}, {"fps", 60},
    {"link-xdbot", 1}, {"max-shift", 30}, {"horizon", 600}, {"show-frame", 1}, {"keep-last-window", 1},
    {"tier-sounds", 1}, {"sound-volume", 0.6}, };
inline double setDef(std::string_view k) { for (auto& d : kSetDefs) if (k == d.k) return d.v; return 0; }
}
inline bool optB(std::string_view k) { auto m = Mod::get(); return m->hasSetting(k) ? m->getSettingValue<bool>(k) : detail::setDef(k) != 0; }
inline double optD(std::string_view k) { auto m = Mod::get(); return m->hasSetting(k) ? m->getSettingValue<double>(k) : detail::setDef(k); }
inline int64_t optI(std::string_view k) { auto m = Mod::get(); return m->hasSetting(k) ? m->getSettingValue<int64_t>(k) : (int64_t)detail::setDef(k); }   // built-in GD font: always present, works on Android
ccColor3B tierColor(int tier);

class Hud : public CCNode {
public:
    static Hud* create();
    ~Hud() override;
    bool init() override;
    void relayout();                 // apply corner + scale settings
    void setFrame(int frame);
    void setStatus(const std::string& text, ccColor3B col, float showSeconds = 0.f);   // analysis progress / errors, top-left
    void onClick(int tier, double frames, double ms, bool sub, double gap);   // pulse + counters
    void resetCounts();
private:
    CCLabelBMFont* m_frame = nullptr;
    CCLabelBMFont* m_info = nullptr;
    CCLabelBMFont* m_stat = nullptr;
    std::array<CCLabelBMFont*, kTierCount> m_rows{};      // tier name (left, fixed x)
    std::array<CCLabelBMFont*, kTierCount> m_nums{};      // tier count (right, fixed x) - separate so numbers never shift
    std::array<int, kTierCount> m_counts{};
    CCNode* m_box = nullptr;
};

std::string fmtFrames(const Window& w);                          // "3" or "0.412"
void spawnRing(PlayLayer* pl, int frame, bool down, bool player2, const Window* w, double fps);   // thick circle + exact frame around the player (click or release)
void clearRings();

enum class Phase { Idle, Recording, Scanning, Done, Failed };

// Global state shared by the panel, HUD and hooks.
struct State {
    static State& get() { static State s; return s; }

    Macro macro;
    std::string macroName, macroPath;
    std::vector<Window> windows;        // one per P1/P2 "down" input, in macro order
    std::vector<size_t> clickInput;     // windows[i] belongs to macro.inputs[clickInput[i]]
    Summary summary;

    bool playing = false;
    size_t next = 0;                    // next input to inject while playing
    size_t nextInput = 0;               // follow mode: next macro input (for release rings)
    size_t nextClick = 0;               // next window to announce on HUD
    int lastFrame = -1;                 // follow-mode frame tracking
    bool xdbotLoaded() const;
    bool sendToXdbot();                 // copy current macro into xdBot's macro folder
    std::filesystem::path xdbotFolder();
    void announce(size_t ordinal);
    std::vector<Input> run;             // inputs actually injected this run (normal or shifted)

    // analysis
    Phase phase = Phase::Idle;
    float progress = 0.f;
    std::string status = "No macro loaded";
    bool simulating = false;            // true while the analyser is stepping the level itself
    bool simDied = false;
    bool simDone = false;               // the simulated run reached the end of the level (counts as "survived")
    bool stepping = false;              // true only while the analyser itself steps the level (the game's own updates are muted meanwhile)
    int  simHazard = -1;                // object id of the hazard that ended the last simulated run (diagnostics)
    double simDieX = 0.0, simDieY = 0.0;
    double simDeathFrac = 0.0;
    float simPrevX = 0.f;
    double simGap = 0.0;
    bool hiMode = false;                // analyser is stepping at 240*hiMul TPS (sub-frame refinement)
    int  hiMul = 1; long hiStep = 0;
    bool playAfter = false;             // PLAY pressed on an un-analysed macro: analyse first, then play
    bool levelMatches(std::string& why);
    void analyzeAndPlay();
    bool starting = false;              // a start (resume / level load) is in flight
    int  pending = 0;                   // action to run once the level is loaded: 1 = analyse, 2 = play
    bool enterLevel(int action);        // open the macro's level (must be saved on this device)

    Hud* hud = nullptr;

    void clearResults();
    bool loadMacro(const std::filesystem::path& p);
    bool loadCache();
    void saveCache();
    std::filesystem::path cachePath() const;
    std::vector<std::filesystem::path> findMacros();      // xdBot folder + XFC folder
    bool ensureMacro();                                    // a macro is loaded (auto-loads the remembered / first one)
    std::string lastMacro() const;
    std::filesystem::path xfcDir();
    void startPlayback();
    void stopPlayback();
    void startAnalysis();                                  // needs a running PlayLayer
};

int currentFrame(GJBaseGameLayer* gl);
double activeTps();                     // TPS the physics should run at right now
void applyFps();                        // apply the custom FPS setting
inline constexpr int kTpsPresets[] = {30, 60, 120, 240, 360, 480, 720, 960, 1200, 2400, 4800};
inline constexpr int kFpsPresets[] = {30, 60, 90, 120, 144, 165, 240, 360, 480, 1000};
void openPanel();
void onLevelReady(PlayLayer* pl);        // called when a level finished loading (auto start after entering the level)
void playTierSound(int tier);            // sound of the click window tier (0..6), see Sound.cpp
void stopTierSounds();

} // namespace xfc
