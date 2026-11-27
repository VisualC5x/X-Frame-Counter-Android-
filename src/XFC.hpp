#pragma once
#include <Geode/Geode.hpp>
#include "Analyzer.hpp"
#include "GDR.hpp"
#include <filesystem>

using namespace geode::prelude;

namespace xfc {

inline constexpr float kStep = 1.f / 240.f;

// Theme: blue / black
inline constexpr ccColor3B kAccent{0, 170, 255};
inline constexpr ccColor3B kAccent2{60, 230, 255};
inline constexpr ccColor3B kPanelBg{5, 9, 20};

inline const char* font() { return "bigFont.fnt"; }   // built-in GD font: always present, works on Android
ccColor3B tierColor(int tier);

class Hud : public CCNode {
public:
    static Hud* create();
    bool init() override;
    void relayout();                 // apply corner + scale settings
    void setFrame(int frame);
    void onClick(int tier, double frames, double ms, bool sub, double gap);   // pulse + counters
    void resetCounts();
private:
    CCLabelBMFont* m_frame = nullptr;
    CCLabelBMFont* m_info = nullptr;
    std::array<CCLabelBMFont*, 7> m_rows{};      // tier name (left, fixed x)
    std::array<CCLabelBMFont*, 7> m_nums{};      // tier count (right, fixed x) - separate so numbers never shift
    std::array<int, 7> m_counts{};
    CCNode* m_box = nullptr;
};

std::string fmtFrames(const Window& w);                          // "3" or "0.412"
void spawnRing(PlayLayer* pl, const Window& w, double fps);       // coloured circle + frame label around the player
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
    double simDeathFrac = 0.0;
    float simPrevX = 0.f;
    double simGap = 0.0;
    bool hiMode = false;                // analyser is stepping at 240*hiMul TPS (sub-frame refinement)
    int  hiMul = 1; long hiStep = 0;
    bool playAfter = false;             // PLAY pressed on an un-analysed macro: analyse first, then play
    bool levelMatches(std::string& why);
    void analyzeAndPlay();

    Hud* hud = nullptr;

    void clearResults();
    bool loadMacro(const std::filesystem::path& p);
    bool loadCache();
    void saveCache();
    std::filesystem::path cachePath() const;
    std::vector<std::filesystem::path> findMacros();      // xdBot folder + XFC folder
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

} // namespace xfc
