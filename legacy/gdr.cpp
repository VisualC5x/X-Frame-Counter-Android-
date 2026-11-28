#include "gdr.hpp"
#include "msgpack.hpp"
#include <algorithm>
#include <cmath>

namespace xfc {

bool parseGdr(const std::vector<uint8_t>& data, Macro& out, std::string& err) {
    if (data.empty()) { err = "Empty file"; return false; }
    mp::Value root;
    if (!mp::decode(data.data(), data.size(), root, err)) return false;
    if (root.type != mp::Value::Type::Map) { err = "Not a .gdr (MessagePack map expected)"; return false; }

    out = Macro{};
    if (auto f = root.get("framerate"); f && f->isNum()) out.fps = f->num();
    else if (auto f2 = root.get("fps"); f2 && f2->isNum()) out.fps = f2->num();

    if (auto lv = root.get("level"); lv && lv->type == mp::Value::Type::Map)
        if (auto id = lv->get("id"); id && id->isNum()) out.levelId = static_cast<int>(id->num());

    auto in = root.get("inputs");
    if (!in || in->type != mp::Value::Type::Arr) { err = "Not a .gdr (no inputs array)"; return false; }

    for (auto const& e : in->arr) {
        if (e.type != mp::Value::Type::Map) continue;
        auto fr = e.get("frame");
        if (!fr || !fr->isNum()) continue;
        Input i;
        i.frame = static_cast<int>(std::llround(fr->num()));
        if (auto b = e.get("btn"); b && b->isNum()) i.button = static_cast<int>(b->num());
        if (auto p = e.get("2p"); p && p->type == mp::Value::Type::Bool) i.p2 = p->b;
        if (auto d = e.get("down"); d && d->type == mp::Value::Type::Bool) i.down = d->b;
        if (i.frame < 0) continue;
        out.inputs.push_back(i);
    }
    if (out.inputs.empty()) { err = "Macro has no inputs"; return false; }
    std::stable_sort(out.inputs.begin(), out.inputs.end(),
                     [](Input const& a, Input const& b) { return a.frame < b.frame; });
    return true;
}

} // namespace xfc
