#pragma once
// Minimal, dependency-free reader for .gdr macros (MessagePack, as written by xdBot / Geode gdr).
// Only the fields XFC needs are decoded; everything else (e.g. 24k frameFixes) is skipped.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace xfc {

struct Input {
    uint32_t frame = 0;
    int      button = 1;
    bool     down = false;
    bool     player2 = false;
};

struct Macro {
    std::string author, botName, botVersion, levelName;
    int         levelId = 0;
    double      fps = 240.0;
    double      duration = 0.0;
    double      gameVersion = 0.0;
    std::vector<Input> inputs;   // sorted by frame
    std::string error;           // empty on success
    bool ok() const { return error.empty(); }
};

namespace detail {
struct Rd {
    const uint8_t* p; const uint8_t* e;
    bool bad = false;
    uint64_t be(int n) { if (e - p < n) { bad = true; p = e; return 0; } uint64_t v = 0; while (n--) v = (v << 8) | *p++; return v; }
    uint8_t u8() { return (uint8_t)be(1); }
    double flt(int n) {
        if (n == 4) { uint32_t u = (uint32_t)be(4); float f; std::memcpy(&f, &u, 4); return f; }
        uint64_t u = be(8); double d; std::memcpy(&d, &u, 8); return d;
    }
    void adv(uint64_t n) { if ((uint64_t)(e - p) < n) { bad = true; p = e; } else p += n; }
    std::string str(uint64_t n) { if ((uint64_t)(e - p) < n) { bad = true; p = e; return {}; } std::string s((const char*)p, n); p += n; return s; }

    // Generic scalar read. kind: 0 nil/other, 1 int, 2 float, 3 bool, 4 str
    struct V { int kind = 0; int64_t i = 0; double f = 0; bool b = false; std::string s; };
    // returns container size via `n` for arrays/maps: kind 5 = array, 6 = map
    V head(uint64_t& n) {
        V v; n = 0; if (p >= e) { bad = true; return v; }
        uint8_t c = u8();
        if (c <= 0x7f) { v.kind = 1; v.i = c; }
        else if (c >= 0xe0) { v.kind = 1; v.i = (int8_t)c; }
        else if (c >= 0x80 && c <= 0x8f) { v.kind = 6; n = c & 15; }
        else if (c >= 0x90 && c <= 0x9f) { v.kind = 5; n = c & 15; }
        else if (c >= 0xa0 && c <= 0xbf) { v.kind = 4; v.s = str(c & 31); }
        else switch (c) {
            case 0xc0: break;
            case 0xc2: v.kind = 3; v.b = false; break;
            case 0xc3: v.kind = 3; v.b = true; break;
            case 0xc4: adv(be(1)); break; case 0xc5: adv(be(2)); break; case 0xc6: adv(be(4)); break;
            case 0xca: v.kind = 2; v.f = flt(4); break;
            case 0xcb: v.kind = 2; v.f = flt(8); break;
            case 0xcc: v.kind = 1; v.i = (int64_t)be(1); break;
            case 0xcd: v.kind = 1; v.i = (int64_t)be(2); break;
            case 0xce: v.kind = 1; v.i = (int64_t)be(4); break;
            case 0xcf: v.kind = 1; v.i = (int64_t)be(8); break;
            case 0xd0: v.kind = 1; v.i = (int8_t)be(1); break;
            case 0xd1: v.kind = 1; v.i = (int16_t)be(2); break;
            case 0xd2: v.kind = 1; v.i = (int32_t)be(4); break;
            case 0xd3: v.kind = 1; v.i = (int64_t)be(8); break;
            case 0xd9: v.kind = 4; v.s = str(be(1)); break;
            case 0xda: v.kind = 4; v.s = str(be(2)); break;
            case 0xdb: v.kind = 4; v.s = str(be(4)); break;
            case 0xdc: v.kind = 5; n = be(2); break;
            case 0xdd: v.kind = 5; n = be(4); break;
            case 0xde: v.kind = 6; n = be(2); break;
            case 0xdf: v.kind = 6; n = be(4); break;
            default: bad = true; break;
        }
        return v;
    }
    void skip() {
        uint64_t n; V v = head(n);
        if (bad) return;
        if (v.kind == 5) for (uint64_t i = 0; i < n && !bad; i++) skip();
        else if (v.kind == 6) for (uint64_t i = 0; i < n && !bad; i++) { skip(); skip(); }
    }
    double num(const V& v) { return v.kind == 2 ? v.f : (double)v.i; }
};
} // namespace detail

inline Macro parseMacro(const uint8_t* data, size_t size) {
    using namespace detail;
    Macro m; Rd r{data, data + size};
    uint64_t n; auto root = r.head(n);
    if (root.kind != 6) { m.error = "Not a .gdr (MessagePack map expected)"; return m; }
    for (uint64_t k = 0; k < n && !r.bad; k++) {
        uint64_t dn; auto key = r.head(dn);
        if (key.kind != 4) { r.skip(); r.skip(); continue; }
        const std::string& s = key.s;
        if (s == "framerate" || s == "duration" || s == "gameVersion") {
            auto v = r.head(dn); double d = r.num(v);
            if (s == "framerate") m.fps = d; else if (s == "duration") m.duration = d; else m.gameVersion = d;
        } else if (s == "author") { m.author = r.head(dn).s; }
        else if (s == "bot") {
            uint64_t bn; auto b = r.head(bn);
            for (uint64_t i = 0; i < bn && b.kind == 6; i++) {
                auto bk = r.head(dn); auto bv = r.head(dn);
                if (bk.s == "name") m.botName = bv.s; else if (bk.s == "version") m.botVersion = bv.s;
            }
        } else if (s == "level") {
            uint64_t ln; auto l = r.head(ln);
            for (uint64_t i = 0; i < ln && l.kind == 6; i++) {
                auto lk = r.head(dn); auto lv = r.head(dn);
                if (lk.s == "name") m.levelName = lv.s; else if (lk.s == "id") m.levelId = (int)lv.i;
            }
        } else if (s == "inputs") {
            uint64_t cnt; auto a = r.head(cnt);
            if (a.kind != 5) { r.skip(); continue; }
            m.inputs.reserve(cnt);
            for (uint64_t i = 0; i < cnt && !r.bad; i++) {
                uint64_t fn; auto o = r.head(fn);
                if (o.kind != 6) { continue; }
                Input in;
                for (uint64_t j = 0; j < fn; j++) {
                    auto fk = r.head(dn); auto fv = r.head(dn);
                    if (fk.s == "frame") in.frame = (uint32_t)fv.i;
                    else if (fk.s == "btn") in.button = (int)fv.i;
                    else if (fk.s == "down") in.down = fv.b;
                    else if (fk.s == "2p") in.player2 = fv.b;
                }
                m.inputs.push_back(in);
            }
        } else r.skip();
    }
    if (r.bad) m.error = "Corrupted .gdr (unexpected end / bad MessagePack)";
    else if (m.inputs.empty()) m.error = "Macro has no inputs";
    return m;
}

inline Macro loadMacroFile(const std::string& path) {
    Macro m;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".gdr") { m.error = "Only .gdr files are accepted"; return m; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { m.error = "Cannot open file"; return m; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    return parseMacro(buf.data(), buf.size());
}

} // namespace xfc
