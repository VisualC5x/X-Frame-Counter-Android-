# XFC 2.2.9 — X Frame Counter

Geode mod for **Geometry Dash 2.2074** (Geode 4.10). Built on the supplied CMake template (pinned bindings, CI untouched).

Shows how many frames of leeway every click of a `.gdr` macro really has, using trajectory re-simulation against the level's hazards.

## Features
- **Frame counter** top-right (corner / scale configurable); HUD uses fixed-width columns so numbers never jump.
- **Coloured circle around every click** with its frame window above it (sub-frame windows down to 0.001).
- **Spam support**: a click can only move between its neighbours (previous release / own release).
- **xdBot link**: macro folder shared, *Send to xdBot* button, and *Link* mode where XFC follows the frame counter while xdBot plays the same macro.
- **QOL-style menu**: round X button in every level opens a 6-tab menu (Macros / Analysis / Visuals / Colors / xdBot / Settings) with toggles, sliders, steppers.
- **7 colour tiers**: `0.001-1`, `2`, `3`, `4-5`, `6-8`, `9-15`, `16-30` frames — live counts per tier, colours editable in mod settings.
- **Per-click window** in frames and **ms** (popup on every click during playback).
- **Trajectory analysis**: each click is moved earlier/later frame by frame from checkpoints; the window is how far it can move before hitting a hazard (checked 90 frames ahead). A 2-frame click means exactly 2 frames work.
- **Modern animated panel** (blue/black): Macros / Analysis / Settings tabs.
- **.gdr only**: upload with the file picker, or pick from xdBot's macro folder (read from xdBot's `macros_folder` setting). Play / Stop / Analyze buttons.
- Analysis is cached per macro.
- Custom XFC icon, menu button and round X button in the Pause menu.

## Use
1. Open the XFC icon on the main menu, or the round X in the pause menu.
2. MACROS → UPLOAD .GDR (or LOAD from the list).
3. Enter the macro's level, press ANALYZE (progress in the ANALYSIS tab), then PLAY.

## Build
Push to GitHub → Actions → *Build XFC 2.2.9* (Windows, SDK v4.10.0, pinned bindings) → download `XFC-2.2.9-Win64` and copy the `.geode` into `Geometry Dash/geode/mods`.
Local: set `GEODE_SDK`, run `geode build`. See `docs/BINDINGS-AND-BUILD.md`.

- **TPS / FPS control** in the SPEED tab (own TPS bypass, same hook method as xdBot; custom FPS).
- **Sub-frame windows**: for clicks where only one frame works, XFC re-simulates the same checkpoint at 240x64 TPS and bisects the click position, so windows are reported down to 0.001 f.
- **All macros from xdBot**: every `.gdr` in xdBot's macro folder (plus XFC's own folder / uploads) is listed; each row has LOAD / ANALYZE / PLAY (PLAY analyses first if needed, then plays with the frame windows). Upload uses the Windows file picker, `.gdr` only.

## xdBot link (how it works)
xdBot has no public API (its source exposes none), so XFC links through what both share: the macro folder and the frame counter. Analyze once in XFC → *Send Macro to xdBot* → in xdBot load and play it. With **Link with xdBot** on, XFC fires circles / HUD on every analysed click by frame.

## Notes
- xdBot has no public API; XFC reads its folder and plays macros itself (don't run xdBot playback/recording at the same time).
- Macros must be 240 fps and match the level.
- Tested off-game: `.gdr` parser (`tests/dump_macro.cpp`) and tier logic (`tests/test_core.cpp`). In-game code compiles only through CI.

## فارسی
XFC نسخه 2.2.9 — شمارنده‌ی دقیق فریم کلیک برای ماکروهای `.gdr` در GD 2.2074. هر کلیک با شبیه‌سازی ترجکتوری تا برخورد با هازارد جابه‌جا می‌شود و پنجره‌ی مجاز (فریم و میلی‌ثانیه) در ۷ سطح رنگی نشان داده می‌شود. فقط `.gdr`؛ آپلود یا خواندن از پوشه‌ی ماکروهای xdBot.
