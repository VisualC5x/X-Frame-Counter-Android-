// Tier sounds: every analysed click plays the sound of its colour tier (tier-1 = hardest window ... tier-7 = widest).
// Lookup order for tier N:  <config dir>/sounds/tier-N.(ogg|mp3|wav)   (your own file wins)  ->  built-in resources/tier-N.wav
// Playback uses FMOD directly (own channel group, cached samples) - the same method as Frame Window Counter's SoundManager.
#include "XFC.hpp"
#include <Geode/fmod/fmod.hpp>
#include <unordered_map>

namespace xfc {

static std::filesystem::path findSound(int tier) {
    auto name = fmt::format("tier-{}", tier + 1);
    std::filesystem::path dirs[2] = { Mod::get()->getConfigDir() / "sounds", Mod::get()->getResourcesDir() };
    for (auto& d : dirs) {
        for (auto ext : {".ogg", ".mp3", ".wav"}) {
            auto p = d / (name + ext);
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) return p;
        }
    }
    return {};
}

static std::unordered_map<std::string, FMOD::Sound*> g_sounds;
static FMOD::ChannelGroup* g_group = nullptr;

void playTierSound(int tier) {
    if (tier < 0 || tier >= (int)kTierCount) return;
    tier = tier * 7 / (int)kTierCount;                 // 12 window buckets share the 7 sound files
    auto mod = Mod::get();
    if (!optB("tier-sounds")) return;

    static std::array<std::filesystem::path, 7> cache;
    static std::array<bool, 7> looked{};
    if (!looked[tier]) { cache[tier] = findSound(tier); looked[tier] = true; }
    if (cache[tier].empty()) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;
    FMOD::System* sys = engine->m_system;
    if (!g_group && sys->createChannelGroup("XFCTierSFX", &g_group) != FMOD_OK) { g_group = nullptr; return; }

    float vol = (float)optD("sound-volume");
    g_group->setVolume(vol);

    auto path = cache[tier].string();
    FMOD::Sound* snd = nullptr;
    auto it = g_sounds.find(path);
    if (it != g_sounds.end()) snd = it->second;
    else {
        if (sys->createSound(path.c_str(), FMOD_DEFAULT | FMOD_LOOP_OFF | FMOD_CREATESAMPLE, nullptr, &snd) != FMOD_OK || !snd) return;
        g_sounds[path] = snd;
    }
    FMOD::Channel* ch = nullptr;
    if (sys->playSound(snd, g_group, false, &ch) == FMOD_OK && ch) {
        ch->setMode(FMOD_LOOP_OFF);
        ch->setPriority(0);
        ch->setVolumeRamp(false);
    }
}

void stopTierSounds() { if (g_group && g_group->stop() != FMOD_OK) g_group = nullptr; }

} // namespace xfc
