#pragma once
// Minimal MessagePack .gdr reader (xdBot / Geode gdr format)
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

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
    std::vector<Input> inputs;
    std::string error;
    bool ok() const { return error.empty() && !inputs.empty(); }
};

namespace detail {
struct Rd {
    const uint8_t* p; const uint8_t* e;
    bool bad = false;
    uint64_t be(int n) {
        if (e - p < n) { bad = true; p = e; return 0; }
        uint64_t v = 0; while (n--) v = (v << 8) | *p++; return v;
    }
    uint8_t u8() { return (uint8_t)be(1); }
    void adv(uint64_t n) { if ((uint64_t)(e - p) < n) { bad = true; p = e; } else p += n; }
    std::string str(uint64_t n) {
        if ((uint64_t)(e - p) < n) { bad = true; p = e; return {}; }
        std::string s((const char*)p, (size_t)n); p += n; return s;
    }

    struct V { int kind = 0; int64_t i = 0; double f = 0; bool b = false; std::string s; };
    // kind: 0 other, 1 int, 2 float, 3 bool, 4 str, 5 array, 6 map

    V head(uint64_t& n) {
        V v; n = 0;
        if (p >= e) { bad = true; return v; }
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
            case 0xc4: adv(be(1)); break;
            case 0xc5: adv(be(2)); break;
            case 0xc6: adv(be(4)); break;
            case 0xc7: { uint64_t l = be(1); adv(1 + l); break; }     // ext 8/16/32: type byte + data (skipped)
            case 0xc8: { uint64_t l = be(2); adv(1 + l); break; }
            case 0xc9: { uint64_t l = be(4); adv(1 + l); break; }
            case 0xd4: adv(1 + 1); break;                              // fixext 1/2/4/8/16
            case 0xd5: adv(1 + 2); break;
            case 0xd6: adv(1 + 4); break;
            case 0xd7: adv(1 + 8); break;
            case 0xd8: adv(1 + 16); break;
            case 0xca: {
                if (e - p < 4) { bad = true; break; }
                float f; std::memcpy(&f, p, 4);
                // bytes are big-endian; swap on little-endian hosts
                uint8_t b[4] = { p[3], p[2], p[1], p[0] };
                std::memcpy(&f, b, 4);
                p += 4; v.kind = 2; v.f = f; break;
            }
            case 0xcb: {
                if (e - p < 8) { bad = true; break; }
                uint8_t b[8] = { p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0] };
                double d; std::memcpy(&d, b, 8); p += 8; v.kind = 2; v.f = d; break;
            }
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
    // read one value; arrays / maps are skipped completely so the reader can never lose its place
    V scalar() {
        uint64_t n; V v = head(n);
        if (bad) return v;
        if (v.kind == 5) { for (uint64_t i = 0; i < n && !bad; i++) skip(); v.kind = 0; }
        else if (v.kind == 6) { for (uint64_t i = 0; i < n && !bad; i++) { skip(); skip(); } v.kind = 0; }
        return v;
    }
    double num(const V& v) const {
        if (v.kind == 2) return v.f;
        if (v.kind == 3) return v.b ? 1.0 : 0.0;
        return (double)v.i;
    }
    bool asBool(const V& v) const {
        if (v.kind == 3) return v.b;
        if (v.kind == 1) return v.i != 0;
        if (v.kind == 2) return v.f != 0.0;
        return false;
    }
};
} // namespace detail

inline Macro parseMacro(const uint8_t* data, size_t size) {
    using namespace detail;
    Macro m;
    if (!data || size < 2) { m.error = "Empty file"; return m; }
    if (data[0] == '{' || data[0] == '[') { m.error = "This is a JSON macro - export it as a binary .gdr from xdBot"; return m; }
    Rd r{data, data + size};
    uint64_t n; auto root = r.head(n);
    if (root.kind != 6) { m.error = "Not a .gdr (MessagePack map expected)"; return m; }
    for (uint64_t k = 0; k < n && !r.bad; k++) {
        uint64_t dn; auto key = r.head(dn);
        if (key.kind != 4) {                         // non-string key: drop its value and go on
            if (key.kind == 5) for (uint64_t z = 0; z < dn && !r.bad; z++) r.skip();
            else if (key.kind == 6) for (uint64_t z = 0; z < dn && !r.bad; z++) { r.skip(); r.skip(); }
            r.skip(); continue;
        }
        const std::string s = key.s;
        if (s == "framerate" || s == "duration" || s == "gameVersion") {
            auto v = r.scalar();
            double d = r.num(v);
            if (s == "framerate") m.fps = d;
            else if (s == "duration") m.duration = d;
            else m.gameVersion = d;
        } else if (s == "author") {
            m.author = r.scalar().s;
        } else if (s == "bot" || s == "level") {
            uint64_t bn; auto b = r.head(bn);
            if (b.kind == 5) { for (uint64_t z = 0; z < bn && !r.bad; z++) r.skip(); continue; }
            if (b.kind != 6) continue;               // scalar: already consumed
            for (uint64_t i = 0; i < bn && !r.bad; i++) {
                auto bk = r.scalar(); auto bv = r.scalar();
                if (s == "bot") {
                    if (bk.s == "name") m.botName = bv.s;
                    else if (bk.s == "version") m.botVersion = bv.s;
                } else {
                    if (bk.s == "name") m.levelName = bv.s;
                    else if (bk.s == "id") m.levelId = (int)r.num(bv);
                }
            }
        } else if (s == "inputs") {
            uint64_t cnt; auto a = r.head(cnt);
            if (a.kind != 5) { if (a.kind == 6) for (uint64_t z = 0; z < cnt && !r.bad; z++) { r.skip(); r.skip(); } continue; }
            m.inputs.reserve((size_t)std::min<uint64_t>(cnt, 1u << 22));
            for (uint64_t i = 0; i < cnt && !r.bad; i++) {
                uint64_t fn; auto o = r.head(fn);
                if (o.kind == 5) { for (uint64_t z = 0; z < fn && !r.bad; z++) r.skip(); continue; }
                if (o.kind != 6) continue;
                Input in; bool haveFrame = false;
                for (uint64_t j = 0; j < fn && !r.bad; j++) {
                    auto fk = r.scalar(); auto fv = r.scalar();
                    if (fk.kind != 4) continue;
                    if (fk.s == "frame") { double d = r.num(fv); in.frame = d < 0 ? 0u : (uint32_t)d; haveFrame = true; }
                    else if (fk.s == "btn" || fk.s == "button") in.button = (int)r.num(fv);
                    else if (fk.s == "down" || fk.s == "d") in.down = r.asBool(fv);
                    else if (fk.s == "2p" || fk.s == "player2") in.player2 = r.asBool(fv);
                }
                if (haveFrame) m.inputs.push_back(in);
            }
        } else {
            r.skip();
        }
    }
    // A file cut off in the middle still gives every input that was read; only a macro with nothing usable is an error.
    if (m.inputs.empty()) m.error = r.bad ? "Corrupted .gdr (bad MessagePack)" : "Macro has no inputs (0 clicks found)";
    // STABLE sort: two inputs on the same frame (release + press of a spam, P1 + P2) must keep their recorded order
    std::stable_sort(m.inputs.begin(), m.inputs.end(),
        [](const Input& a, const Input& b) { return a.frame < b.frame; });
    return m;
}

inline bool endsWithGdr(std::string path) {
    for (auto& c : path) c = (char)std::tolower((unsigned char)c);
    return path.size() >= 4 && path.substr(path.size() - 4) == ".gdr";
}

inline Macro loadMacroFile(const std::string& path) {
    Macro m;
    if (!endsWithGdr(path)) {
        m.error = "Only .gdr files are accepted";
        return m;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) { m.error = "Cannot open file: " + path; return m; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    if (buf.empty()) { m.error = "File is empty"; return m; }
    return parseMacro(buf.data(), buf.size());
}

} // namespace xfc
