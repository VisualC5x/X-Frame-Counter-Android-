#!/bin/sh
# Local unit tests for the pure logic (no Geode needed).
set -e
cd "$(dirname "$0")/.."
g++ -std=c++20 -Wall -Wextra -Werror tests/test_window_math.cpp legacy/window_math.cpp -o /tmp/t_wm && /tmp/t_wm
g++ -std=c++20 -Wall -Wextra -Werror tests/test_engine.cpp legacy/engine.cpp legacy/analysis.cpp legacy/gdr.cpp legacy/msgpack.cpp legacy/window_math.cpp -o /tmp/t_en && /tmp/t_en
g++ -std=c++20 -Wall -Wextra -Werror tests/test_gdr.cpp -o /tmp/t_gdr && /tmp/t_gdr
g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/test_core.cpp -o /tmp/t_core && /tmp/t_core
