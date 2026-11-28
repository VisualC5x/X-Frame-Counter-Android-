#include "msgpack.hpp"
#include <cstring>

namespace mp {

const Value* Value::get(const std::string& key) const {
    if (type != Type::Map) return nullptr;
    for (size_t k = 0; k < keys.size(); ++k)
        if (keys[k] == key) return &arr[k];
    return nullptr;
}

namespace {

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    std::string err;

    bool fail(const char* m) { if (err.empty()) err = m; return false; }
    bool need(size_t k) { return (n - pos >= k) ? true : fail("Corrupted .gdr (unexpected end of data)"); }

    bool u(size_t bytes, uint64_t& out) {
        if (!need(bytes)) return false;
        out = 0;
        for (size_t k = 0; k < bytes; ++k) out = (out << 8) | p[pos + k];
        pos += bytes;
        return true;
    }

    bool str(size_t len, Value& v, Value::Type t) {
        if (!need(len)) return false;
        v.type = t;
        v.s.assign(reinterpret_cast<const char*>(p + pos), len);
        pos += len;
        return true;
    }

    bool arrayOf(size_t len, Value& v, int depth) {
        if (len > n - pos) return fail("Corrupted .gdr (bad array length)");
        v.type = Value::Type::Arr;
        v.arr.resize(len);
        for (size_t k = 0; k < len; ++k)
            if (!read(v.arr[k], depth + 1)) return false;
        return true;
    }

    bool mapOf(size_t len, Value& v, int depth) {
        if (len > n - pos) return fail("Corrupted .gdr (bad map length)");
        v.type = Value::Type::Map;
        v.arr.resize(len);
        v.keys.resize(len);
        for (size_t k = 0; k < len; ++k) {
            Value key;
            if (!read(key, depth + 1)) return false;
            if (key.type == Value::Type::Str) v.keys[k] = key.s;
            else if (key.type == Value::Type::Int) v.keys[k] = std::to_string(key.i);
            else v.keys[k] = "?";
            if (!read(v.arr[k], depth + 1)) return false;
        }
        return true;
    }

    bool read(Value& v, int depth) {
        if (depth > 64) return fail("Corrupted .gdr (nesting too deep)");
        if (!need(1)) return false;
        uint8_t t = p[pos++];
        uint64_t x = 0;
        if (t <= 0x7f) { v.type = Value::Type::Int; v.i = t; return true; }
        if (t >= 0xe0) { v.type = Value::Type::Int; v.i = static_cast<int8_t>(t); return true; }
        if (t >= 0xa0 && t <= 0xbf) return str(t & 0x1f, v, Value::Type::Str);
        if (t >= 0x90 && t <= 0x9f) return arrayOf(t & 0x0f, v, depth);
        if (t >= 0x80 && t <= 0x8f) return mapOf(t & 0x0f, v, depth);
        switch (t) {
            case 0xc0: v.type = Value::Type::Nil; return true;
            case 0xc2: v.type = Value::Type::Bool; v.b = false; return true;
            case 0xc3: v.type = Value::Type::Bool; v.b = true; return true;
            case 0xc4: if (!u(1, x)) return false; return str(x, v, Value::Type::Bin);
            case 0xc5: if (!u(2, x)) return false; return str(x, v, Value::Type::Bin);
            case 0xc6: if (!u(4, x)) return false; return str(x, v, Value::Type::Bin);
            case 0xca: {
                if (!u(4, x)) return false;
                uint32_t bits = static_cast<uint32_t>(x);
                float f;
                std::memcpy(&f, &bits, 4);
                v.type = Value::Type::Float; v.f = f; return true;
            }
            case 0xcb: {
                if (!u(8, x)) return false;
                double d;
                std::memcpy(&d, &x, 8);
                v.type = Value::Type::Float; v.f = d; return true;
            }
            case 0xcc: if (!u(1, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int64_t>(x); return true;
            case 0xcd: if (!u(2, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int64_t>(x); return true;
            case 0xce: if (!u(4, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int64_t>(x); return true;
            case 0xcf: if (!u(8, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int64_t>(x); return true;
            case 0xd0: if (!u(1, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int8_t>(x); return true;
            case 0xd1: if (!u(2, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int16_t>(x); return true;
            case 0xd2: if (!u(4, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int32_t>(x); return true;
            case 0xd3: if (!u(8, x)) return false; v.type = Value::Type::Int; v.i = static_cast<int64_t>(x); return true;
            case 0xd9: if (!u(1, x)) return false; return str(x, v, Value::Type::Str);
            case 0xda: if (!u(2, x)) return false; return str(x, v, Value::Type::Str);
            case 0xdb: if (!u(4, x)) return false; return str(x, v, Value::Type::Str);
            case 0xdc: if (!u(2, x)) return false; return arrayOf(x, v, depth);
            case 0xdd: if (!u(4, x)) return false; return arrayOf(x, v, depth);
            case 0xde: if (!u(2, x)) return false; return mapOf(x, v, depth);
            case 0xdf: if (!u(4, x)) return false; return mapOf(x, v, depth);
            default: return fail("Corrupted .gdr (unsupported MessagePack type)");
        }
    }
};

} // namespace

bool decode(const uint8_t* data, size_t size, Value& out, std::string& err) {
    Reader r{data, size, 0, {}};
    if (!r.read(out, 0)) { err = r.err.empty() ? "Corrupted .gdr (bad MessagePack)" : r.err; return false; }
    return true;
}

} // namespace mp
