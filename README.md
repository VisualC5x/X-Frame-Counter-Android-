#  X FRAME COUNTER `v2.9.4`
**by LNTB017** · Geode mod · GD 2.2074 · 240 TPS · Windows + Android + iOS

> 🟥 1f · 🟧 2f · 🟨 3f · 🟩 4-5f · 🟦 6-8f · 🔵 9-15f · 🟣 16f+

## Version v2.9.0
-  Release moves with the click, deeper survive-check, every window verified, checkpoint drift detector
-  Solid hollow rings, no fade, no transparency
-  Round icons on the ANALYZE / SETTINGS buttons
-  Build artifact is named by version: `XFC-v2.9.0-Win64`

Full history: [changelog.md](changelog.md)

## Build on GitHub
Push this folder to the repository root. Actions builds `.github/workflows/build.yml` and uploads the `.geode` as the `xfc-win64` artifact.

## Local tests (pure logic, no Geode needed)
`sh tests/run.sh`

## Targeting Geode 5.x / GD 2.2074
Change `geode` and `gd` in `mod.json`, `sdk:` in the workflow (currently v4.10.0), and the `processCommands` hook in `src/main.cpp`
(the 2.2081 signature has two extra bool parameters).
