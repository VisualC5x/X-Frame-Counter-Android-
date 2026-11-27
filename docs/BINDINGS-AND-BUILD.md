# Bindings & build notes — GD 2.2074 / Geode 4.10

## What this template pins

| Piece | Value | Why |
|-------|--------|-----|
| Geode SDK | `v4.10.0` | Matches GD 2.2074 modding era |
| GD version in mod.json | `2.2074` | Target game |
| Bindings repo | `geode-sdk/bindings` | Official addresses & signatures |
| bindings-ref | `c949aeae063175eca528b33cf8f5b573935b0079` | Fixed commit so GitHub Actions does not break overnight |

Bindings are **not** copied into this zip (they are large and updated upstream).  
CI downloads them via `geode-sdk/build-geode-mod` using `bindings-ref`.

## Local build

```bash
# 1. Install Geode CLI + clone SDK
# 2. Set env
export GEODE_SDK=/path/to/geode   # Windows: set GEODE_SDK=D:\geode

# 3. From mod root
geode build
# or
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Checklist to avoid CMake / include errors

1. All `.cpp` under `src/`
2. Headers under `src/` → use `#include "name.hpp"` (not nested paths unless you add that folder to `target_include_directories`)
3. Keep `target_include_directories(... src)` **before and after** `setup_geode_mod`
4. Do not commit a half-copied `bindings/` folder unless you know what you are doing — let CI fetch the pinned ref
5. `mod.json` → `"geode": "4.10.0"` and `"gd": { "win": "2.2074" }`

## Updating bindings later

Only change `bindings-ref` in `.github/workflows/build.yml` when:
- You move to a newer GD version, or
- Geode docs explicitly recommend a newer bindings commit for 2.2074

Find commits: https://github.com/geode-sdk/bindings/commits

## Official links

- Geode docs: https://docs.geode-sdk.org/
- Bindings: https://github.com/geode-sdk/bindings
- SDK: https://github.com/geode-sdk/geode
- build-geode-mod action: https://github.com/geode-sdk/build-geode-mod
