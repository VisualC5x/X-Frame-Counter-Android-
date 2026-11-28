# Changelog

## v2.9.6
<cy>PLAY works on its own</c>
- PLAY starts the macro right away, it no longer waits for (or depends on) the analysis

## v2.9.5
<cy>Menu fixed + macro search</c>
- Menu pages and the macro list are always visible now (own scrolling: drag, or the two arrows next to the close button)
- <cy>Search bar</c> in Macros: type part of a name and the list filters
- ON / OFF written on every switch, tab pictures, new XFC circle icon
- Macros found in the xdBot folder automatically; the last macro is remembered; ANALYZE / PLAY load one by themselves
- Analysis: the game's own updates are muted while XFC re-simulates, clean restart + automatic retry, clearer error text
- Developer name set as <cy>LNTB017</c>

## v2.9.4
<cy>Phone-friendly package</c>
- One combined .geode for <cy>Windows + Android 32/64 + iOS</c>
- Source zip has no .git folder; `github/build.yml` is a visible copy of the workflow

## v2.9.3
<cy>Mobile (Android + iOS)</cy>
- XFC now builds for <cy>Android 32/64-bit</c> and <cy>iOS</c> next to Windows (one combined .geode)
- Bigger touch targets on phones: rows, buttons, tabs and the close button
- Build workflow builds every platform and packs one .geode

## v2.9.2
<cy>Click and release circles</cy>
- <cj>Thick, very clear circle</c> around the player on every <cy>click</c> (big, window colour, <cy>F1234</c> = exact frame, window in frames below it) and on every <cy>release</c> (smaller white circle, <cy>R1250</c> under it) with a dark outline so it reads on any background
- New option: circle on every release

## v2.9.1
<cy>Button icon</c>
- Round button: dark disc, glowing X, plain white rim (no coloured ring)

## v2.9.0
<cy>New menu, new counter, round X buttons</c>

<cg>Changed</c>
- <cj>Counter rows</c> are now exactly <cy>15-20, 10-15, 9-10 ... 1-2, 0-1</c> (12 windows, widest on top), colours like the reference counter
- <cj>Round X button</c> in the main menu and in the pause menu only, the same size as the buttons next to it. The in-level button is gone
- <cj>Completely new menu</c>: dark purple, rounded rows, sidebar tabs, scrolling pages. Nothing can stick out or hide; the whole panel fits the screen
- Old cached analyses are discarded again (the windows are bucketed differently) - press ANALYZE once

## v2.8.2
<cy>Macro reading, exact frames, menu</cy>

<cg>Fixes</c>
- <cj>False "macro dies on its own" error</c>: clicks were sent to the wrong player (the game's `handleButton` takes `isPlayer1`, XFC passed `player2`). Player 1 clicks now go to player 1, player 2 clicks to player 2
- <cj>Frame counter</c> is now the game's own physics-step counter (the same one xdBot stamps on its inputs), saved and restored by checkpoints, so frames match the macro exactly
- <cj>.gdr reader</c> rewritten: never loses its place on unknown / nested fields, same-frame inputs keep their recorded order (spam), accepts every MessagePack type, tells JSON macros apart from broken ones
- <cj>Analysis</c>: held buttons are restored exactly when a checkpoint is loaded (the old run's held state leaked into the next test), a run that reaches the end of the level counts as survived (no more false deaths / end screen)
- <cj>Old cached analyses are discarded</c> (they were made with the wrong click routing) - press ANALYZE once again
- <cj>Menu</c> is 300 high now: it was 330 on a 320-high screen, which pushed the close X out of the screen. X is placed inside the corner
- <cj>In-level X button</c> lives in the same menu as GD's pause button, so it always reacts; the pause-menu XFC button has a fallback if GD's node ids are missing. A stale menu pointer can no longer block opening; a failed open now shows a message

## v2.8.1
<cr>Crash fix</c>
- Fixed a crash when opening a level (touch-priority call on a menu that was not on screen yet)
- The in-level X button is a plain menu button again (no touch-priority tricks)

## v2.8.0
<cy>Stability + macro fixes</c>

<cg>Fixes</c>
- <cj>Crash fixed</c>: circles that faded out were still referenced when the level restarted (crashed after a while). The HUD and panel pointers are now cleared safely, and quitting in the middle of an analysis no longer leaves the game in simulation mode
- <cj>Tier colours, HUD and menu text could turn black / invisible</c> when a setting was missing - every setting now has a built-in default
- <cj>PLAY / ANALYZE from the pause menu</c> now closes the pause menu first (the paused game never ran the macro)
- <cj>PLAY / ANALYZE outside a level</c> opens the macro's level automatically (the level must be saved on this device)
- <cj>No more made-up click windows</c>: a macro is analysed with the real physics before it is played
- Survive-check setting stepped to 240 max; it now goes 60 - 6000 as in the settings file
- Menu icon is bigger and sharp; XFC buttons are sharp in every menu
- The in-level X button no longer sits on top of the pause button

## v2.7.0
<cy>Maximum-accuracy analysis, solid hollow rings, icon buttons</c>

<cg>Accuracy</c>
- <cj>Release moves with the click</c>: press and release shift together, so the hold length stays the same (ship, wave, orbs, holds). Setting: "Move release with the click"
- <cj>Deeper survive-check</c>: a moved click must now survive until <cy>3 later clicks</c> are behind it (was: the next click), up to 6000 frames. Both values are settings
- <cj>Every window is verified</c>: the baseline and both edges are re-run once more, a click that disagrees is marked <cr>~</c>
- <cj>Checkpoint drift detector</c>: after every click the replay from the snapshot must land on the exact same player state. If not, the HUD and the menu warn you
- Window search up to <cy>60 frames</c> each way (was 30)
- Ring text shows the full picture: `5f (-2/+2)` = width, early, late

<co>Visual</c>
- <cj>Rings are solid and hollow</c>: fully opaque colour, black outline, no fade, no transparency, thicker and bigger
- <cj>New round icons</c> on the ANALYZE and SETTINGS buttons
- Menu panel is taller so nothing overlaps

<cb>Release</c>
- Build artifact is named with the version: `XFC-v2.7.0-Win64`
- The workflow stops with a clear message if `mod.json` and `CMakeLists.txt` versions differ

## v2.6.0
<cy>Visual overhaul + safer analysis</c>

<cg>Added</c>
- Big round icon in the XFC menu, close button (X) fully inside the top-left corner of the panel
- Solid, opaque rings with a black outline, no fade
- Before / after window on every ring
- Settings: HUD live frame counter and HUD last-click text can be switched off
- Toggles in the menu for every text option

<co>Changed</c>
- Pause menu button is the normal pause-menu size
- Menu panel is wider, bars and numbers re-aligned
- HUD text is only redrawn when it really changes

<cr>Fixed</c>
- Crash after the level ends or when leaving the level
- Analysis can no longer finish into a level that is already showing the end screen
- Bars sticking out of the analysis panel
- Close button half hidden behind the panel edge

## v2.5.0
<cy>Rebuilt from scratch</c>
- No raw addresses: hooks use Geode bindings only
- Integer-frame windows measured by re-simulation from checkpoints, spam aware
- Ring with the frame count around every click, HUD with tier counts
- Macro level check, 240 fps check, clear error when the macro dies on its own
