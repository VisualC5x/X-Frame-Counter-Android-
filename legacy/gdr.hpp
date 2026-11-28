#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xfc {

struct Input {
    int frame = 0;
    int button = 1;
    bool p2 = false;
    bool down = false;
};

struct Macro {
    double fps = 240.0;
    int levelId = 0;
    std::vector<Input> inputs;   // sorted by frame (stable)
};

bool parseGdr(const std::vector<uint8_t>& data, Macro& out, std::string& err);

} // namespace xfc
