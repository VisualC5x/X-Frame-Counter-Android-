#include "../src/GDR.hpp"
#include <cstdio>
int main(int argc, char** argv) {
    auto m = xfc::loadMacroFile(argv[1]);
    if (!m.ok()) { printf("ERR %s\n", m.error.c_str()); return 1; }
    printf("author=%s bot=%s %s level=%s(%d) fps=%.1f dur=%.2fs gv=%.4f inputs=%zu\n", m.author.c_str(), m.botName.c_str(), m.botVersion.c_str(), m.levelName.c_str(), m.levelId, m.fps, m.duration, m.gameVersion, m.inputs.size());
    for (int i = 0; i < 4; i++) printf(" in[%d] f=%u btn=%d down=%d p2=%d\n", i, m.inputs[i].frame, m.inputs[i].button, m.inputs[i].down, m.inputs[i].player2);
    // hold/gap stats
    int minHold = 1e9, minGap = 1e9; uint32_t lastDown = 0, lastUp = 0;
    for (auto& in : m.inputs) { if (in.player2) continue; if (in.down) { if (lastUp) minGap = std::min<int>(minGap, in.frame - lastUp); lastDown = in.frame; } else { minHold = std::min<int>(minHold, in.frame - lastDown); lastUp = in.frame; } }
    printf("shortest hold=%d frames, shortest gap=%d frames, last input frame=%u\n", minHold, minGap, m.inputs.back().frame);
    return 0;
}
