# <cj>X FRAME COUNTER</c>
<cy>by LNTB017</c>  -  <cg>v2.7.0</c>

<cb>The exact click window of every click in a .gdr macro.</c>
GD 2.2074  -  240 TPS  -  Windows

---

## <co>What it does</c>
Every click is re-played <cj>earlier</c> and <cj>later</c>, frame by frame, until the attempt dies.
What is left is the <cg>real window in frames</c>, drawn as a <cr>s</c><co>o</c><cy>l</c><cg>i</c><cj>d</c> hollow ring right on the click:

- <cg>5f (-2/+2)</c>  = 5 frames wide, 2 early, 2 late, plus the click itself
- <cy>*</c>  = limited by a neighbouring input (spam), not by a hazard
- <cr>X</c>  = the click dies even on its own frame
- <cr>~</c>  = the verification pass disagreed, treat with care

## <co>How to use</c>
1. Pick your macro in the mod settings
2. Open its level and pause
3. Press the round <cj>XFC</c> button
4. Press <cg>ANALYZE</c>, when it ends the macro replays and every click gets its ring

## <co>Colour tiers</c>
<cr>1f</c>   <co>2f</c>   <cy>3f</c>   <cg>4-5f</c>   <cj>6-8f</c>   <cb>9-15f</c>   <cp>16f+</c>

## <co>Why it is exact</c>
- <cj>Press and release move together</c>, the hold length never changes
- <cj>Deep survive-check</c>: a moved click must survive until several later clicks are behind it
- <cj>Every window is verified</c> a second time
- <cj>Drift detector</c>: warns if the game does not restore a snapshot exactly

## <co>Settings</c>
Rings, labels, ms and the HUD can each be switched off. Ring size and lifetime, search distance and survive-check length are adjustable.

---
<cy>Needs a 240 fps macro recorded for the level you are playing.</c>
