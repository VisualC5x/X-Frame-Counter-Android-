// Unit test for src/GDR.hpp (the .gdr MessagePack reader). Build: g++ -std=c++20 -Wall -Wextra tests/test_gdr.cpp -Isrc
#include "../src/GDR.hpp"
#include <cassert>
#include <cstdio>
using namespace xfc;
using B = std::vector<uint8_t>;
static void s(B& b, const char* t) { size_t n = strlen(t); b.push_back(0xa0 | (uint8_t)n); b.insert(b.end(), t, t + n); }
static void in(B& b, uint32_t frame, int btn, bool p2, bool down, bool wide) {
    b.push_back(0x84); s(b, "frame");
    if (wide) { b.push_back(0xce); for (int i = 3; i >= 0; i--) b.push_back((frame >> (8 * i)) & 255); } else b.push_back((uint8_t)frame);
    s(b, "btn"); b.push_back((uint8_t)btn); s(b, "2p"); b.push_back(p2 ? 0xc3 : 0xc2); s(b, "down"); b.push_back(down ? 0xc3 : 0xc2);
}
int main() {
    B b; b.push_back(0x87);
    s(b, "author"); s(b, "me");
    s(b, "bot"); b.push_back(0x82); s(b, "name"); s(b, "xdBot"); s(b, "version"); s(b, "2.0");
    s(b, "framerate"); b.push_back(0xca); b.push_back(0x43); b.push_back(0x70); b.push_back(0); b.push_back(0);   // 240.0f
    s(b, "extra"); b.push_back(0x92); b.push_back(0x81); s(b, "k"); b.push_back(1); b.push_back(5);                // unknown nested value
    s(b, "level"); b.push_back(0x82); s(b, "id"); b.push_back(0xce); b.push_back(0); b.push_back(0x0f); b.push_back(0x42); b.push_back(0x40); s(b, "name"); s(b, "Lvl");
    s(b, "duration"); b.push_back(0xcb); { double d = 12.5; uint8_t x[8]; memcpy(x, &d, 8); for (int i = 7; i >= 0; i--) b.push_back(x[i]); }
    s(b, "inputs"); b.push_back(0x94);
    in(b, 10, 1, false, true, false); in(b, 10, 1, false, false, false);   // same frame: order must survive the sort
    in(b, 300, 1, true, true, true);  in(b, 5, 1, false, true, false);
    Macro m = parseMacro(b.data(), b.size());
    assert(m.ok()); assert(m.inputs.size() == 4);
    assert(m.fps == 240.0 && m.duration == 12.5 && m.levelId == 1000000 && m.levelName == "Lvl" && m.botName == "xdBot" && m.author == "me");
    assert(m.inputs[0].frame == 5);
    assert(m.inputs[1].frame == 10 && m.inputs[1].down && m.inputs[2].frame == 10 && !m.inputs[2].down);   // press stays before release
    assert(m.inputs[3].frame == 300 && m.inputs[3].player2);
    auto cut = parseMacro(b.data(), b.size() - 3);                 // truncated file: keeps what was read
    assert(cut.inputs.size() >= 3);
    assert(!parseMacro((const uint8_t*)"{\"a\":1}", 7).ok());
    assert(!parseMacro((const uint8_t*)"\x81\xa1x\x01", 4).ok());
    std::puts("GDR parser OK");
}
