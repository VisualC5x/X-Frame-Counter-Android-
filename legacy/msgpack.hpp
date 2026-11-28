#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mp {

struct Value {
    enum class Type { Nil, Bool, Int, Float, Str, Bin, Arr, Map };
    Type type = Type::Nil;
    bool b = false;
    int64_t i = 0;
    double f = 0.0;
    std::string s;                 // Str / Bin payload
    std::vector<Value> arr;        // Arr elements, or Map values
    std::vector<std::string> keys; // Map keys (same length as arr)

    bool isNum() const { return type == Type::Int || type == Type::Float; }
    double num() const { return type == Type::Float ? f : static_cast<double>(i); }
    const Value* get(const std::string& key) const;
};

// Decodes one MessagePack value. Bounds-checked, depth-limited.
bool decode(const uint8_t* data, size_t size, Value& out, std::string& err);

} // namespace mp
