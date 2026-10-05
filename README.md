# SpaceMantis

**Roguelike space-career game for M5Stack Core2.**

SpaceMantis is an Elite-like game with the spreadsheet hidden underneath the flight view. There is no galaxy map, no plotted course, no hardpoint screen, and no inventory Tetris. The player flies into things and makes opportunistic decisions.

> **See what is here → commit by flying or one verb → dive or deal → progress ticks → die lost but not erased → until depth is the real map.**

## The rules

- **No course plotting.** Navigation is semantic gates plus subspace depth.
- **No ship-management minigame.** Earned capability is automatically equipped.
- **No quest log.** A station or encounter offers a small local hand of work; accepting it creates one active lead.
- **No galaxy map.** The player knows only what has recently been heard, what gates are visible, and what deep landmarks have become meaningful.
- **Dive, don't jump.** Flying into a named gate is the choice of destination. It spawns the next gate, and the third is a portal. Each portal moves exactly one layer. Resurfacing is a dangerous shortcut.
- **Real space pays experience; the deep pays money.** Nobody keeps coordinates in real space. Subspace connects every point of it, and the deeper you go the more compressed and connected space becomes. A way through the deep is the most valuable thing a pilot can carry.
- **Lost in space is literal.** Destruction reseeds the universe and wipes surface/shallow place knowledge while the pilot, career and fitted capabilities survive. **In fiction the pilot only knows they are lost** — theories about the deep, bad jumps, charts that lie. They never learn that their previous cosmos is gone.
- **The deep becomes smaller.** Deep layers contain a rising fraction of unique and persistent places. Some are authored signatures; others are procedural places that acquire persistence when discovered.

## Flight

### Controls

- **Tilt:** aim the nose, relative to a captured neutral flight pose.
- **Slide:** yaw (left/right) and roll (up/down), quick and precise. It never drags the sky.
- **Throttle:** the slider on the right edge. Drag it; there is a soft detent at cruise. Low is all stop, high is boost.
- **Tap:** target anything in space. Its verbs appear beside it.
- **A:** next target. **B:** re-center the tilt pose. **C:** snap the throttle to cruise.
- **Hold B:** the status screen: a Liminar Transit pilot license (records the escape pod keeps: bank, ranks, fixed points) over a diagnostic of this hull (lost with it: condition, hold, fitted systems, depth rating). A visual reference only; any tap or button closes it, and the world waits.
- **Hold C (on the status screen):** the pilot journal and your active lead. Hold C again for the license.
- **Hold A:** the map, the pilot's memory of subspace lanes drawn as *the deep is small* (real space outside, the Deep Cove at the centre). Places you have been sit on the rim in network order; gates you have seen hang outside them; rumors are tethered to where you heard them; fixed points sit on their rings. Tap a place to trace the remembered way from here. Hold A again for *this system*: star, bodies, dock, every gate here and who built it, or in the deep what the scanner picks up. Visual reference only.
- **Quick taps** of A and B act on release, so a hold never also does the tap.
- **Reticle / alignment:** light assistance exists only when nearly threading a gate or portal: a nudge, not an autopilot.
- **Gate threading:** a gate only counts when the ship actually passes through the ring. Missing one does not select it: the ring stays where it is, so come round and fly it again.

### A trip

1. In real space a few named gates fan out around you. Each shows a place and its **depth**: how deep a dive the way there takes. Names come from rumors, the job you took, places you have been, charted deep landmarks, and places nobody has heard of. A depth marked `!` is past what your hull is rated for.
2. Fly into one. That is the whole decision. The next gate appears ahead, then a **portal** whose inside already shows the next layer.
3. Every portal is one layer down, until the destination's depth. Then the chain turns and every portal is one layer up.
4. You resurface **at the place**, far across the universe. A way through depth 2 or more pays real money on arrival; depth 1 is everyday infrastructure.

Pushing past your depth rating risks a glitch at the portal. A portal that cannot take a dry tank turns the chain back up, and you surface somewhere else. A dry climb costs hull, so nobody is stranded below.

### Travel layers

| Layer | Feel | Who is there |
|---|---|---|
| Real space | restrained stars, dusty nebulae, a local sun, lit worlds and giants | travelers, merchants, security, pirates, stations, asteroids, pods, wrecks |
| The Shallows | dark teal distortion, stars begin to smear | lost travelers, sub-pirates in the liminal wake, anomalies |
| Smuggler Roads | marbled violet, streamers flowing past | sub-pirates, deep traders, wrecks, first landmarks |
| Below the Roads | the sky folds into a kaleidoscope; collapsed stars bend light | hostiles, anomalies, landmarks, afterimages |
| Deep Cove | a tunnel folded into itself, a heartbeat that inverts the sky | the ghost fleet; the whole layer is persistent |

The deep is smaller: gates come closer together the further down you are. The Deep Cove keeps the same local gate names, light and places in every universe. Real-space people barely believe in sub-pirates, never mind the cove.

## Context verbs

Tap a thing and it offers what it can do, given what it is, what your ship can do, and what is going on. There is no menu. Green is talk, red is violence, blue is dock:

| Target | Verbs |
|---|---|
| Ship | HAIL / ATTACK (the deep hostile answers to SIGNAL) |
| Station | DOCK — the docking computer flies you round to the slot |
| Asteroid | MINE (SPENT, HOLD FULL when it can't) |
| Gas giant | SCOOP near the cloud tops (TANK FULL, SETTLING) |
| Escape pod | RESCUE |
| Wreck | SALVAGE / ATTACK |
| Artifact | READ / ATTACK |
| Anomaly | SCAN / ATTACK |
| Deep landmark | CHART |

A verb out of range shows its distance instead. Aggressive contacts don't wait: pirates bend toward you and open fire when close. A good cloak and Ghost rank help you slip them, and boosting away works too.

### HAIL — green

Parley, trade, bluff, request help, rescue, receive rumors, read anomalies.

### ATTACK — red

Combat is a short resolve, not a dogfight simulator. Weapons, shields, career ranks, depth and encounter type alter the result.

### DOCK — blue

Aim for the blue gate in front of the slot, or tap DOCK. The board is thin: refuel/repair and sell/buy are business, and you stay docked. Taking work, buying a rumor or taking the board's opportunity **sets your course**: you are taxied out with that destination's gate waiting right in front of you.

## The money

- Real-space exploring (unheard-of places, hails, rescues) mostly pays **experience**.
- A way through **depth 2+** pays on arrival: roughly 90 / 280 / 700 credits for depth 2 / 3 / 4, half again for a place nobody had heard of.
- **Charting** a deep landmark for the first time pays 70 × depth²: up to about 1,100 credits for the deepest.
- The **ghost fleet** in the Deep Cove trades in fixed points, not goods. They ignore pilots who still count stars.

## Haptics

The Core2's vibration motor is the soundtrack. Layers hum and throb: a tidal pulse near heavy bodies, a tearing hum that builds as a portal approaches, the pressure of subspace, a heartbeat below the roads. One-shots pop and thud: weapon fire, hits, the thread of a gate, the purr of docking. On a portal crossing the hum peaks, then cuts to total silence.

## Hidden spreadsheet

The firmware tracks:

- eight career tracks and rank/XP
- weapon, shield, mining, scanner, trailer, stabilizer, bulkhead and cloak capabilities
- fuel, hull and hold bulk
- local market weather and commodity volatility
- security/pirate/house/under pressure
- rumors and known gates with local expiry
- emergent story flags
- universe seed
- depth/glitch tolerance
- persistent deep landmarks
- procedural encounter weighting
- opportunistic contracts and their consequences

The UI exposes consequences, not the table.

## Careers

- **Hauler** — cargo capacity and supply work
- **Gun Hand** — weapons and bounty pressure
- **Prospector** — mining yield and landmark extraction
- **Rescuer** — survival and escort/rescue opportunities
- **Trader** — market access, fuel economy and commerce
- **Wanderer** — unknown gates, scanner quality and discovery
- **Depth Runner** — bulkheads, deep survival and persistent landmarks
- **Ghost** — cloak, security pressure and quiet work

Ranks quietly modify capabilities and the kinds of encounters/offers the universe supplies.

## Persistence

A destruction is a universe wipe, not career permadeath.

**Retained:**

- career ranks and XP
- earned capabilities
- credits
- persistent story flags
- discovered deep landmarks

**Wiped:**

- rumors
- known surface/shallow gates
- regular routes
- current haul
- local heat

A deep landmark is not truly persistent merely because it exists in the procedural table. The pilot has to **chart it**; charting writes a persistent flag with the life it was charted in. Charted landmarks appear as destination gates in any later universe. The first time a pilot charts a place they already charted **in a previous life**, the game shows its one ending card, THE DEEP IS SMALL, and then they keep flying.

## Content model

SpaceMantis uses compact card-sized content rather than dialogue trees. Procedural names, encounter flavor, station opportunities and deep story beats are combined with flags and depth bands to create emergent continuity.

The game is deliberately capable of producing lines such as:

- a traveler asking whether you know the way back
- a merchant selling a place-name
- a patrol caring more about your gate label than your cargo
- a sub-pirate noticing your resurfacing wake
- a scanner revealing an impossible geometry
- a deep landmark whose name survives a later universe wipe

These are consequences and clues, not a quest log.

## Current content families

The current encounter deck includes travelers, merchants, security, surface pirates, sub-pirates, hostile deep contacts, anomalies, asteroids, gas giants, stations/outposts, escape pods, wrecks, deep artifacts and the ghost fleet. Everything uses the same interaction language: tap it, choose the verb it offers, resolve, keep flying.

The station board includes haul, bounty, rescue, survey, smuggle, tourism, outpost supply, escort and market work, plus ghost runs, depth runs and landmark watches from the opportunity board. Work only becomes active when the player commits to it; merely looking at an offer has no cargo side effect.

## Repository / deployment

The project keeps the supplied Mantis GitHub/PlatformIO structure and the supplied Pages workflow. The workflow consumes a root project ZIP when present, replaces `src/` as a payload, builds `m5stack-core2`, stages the merged firmware, and deploys the web flasher to GitHub Pages.

The workflow directory is intentionally preserved exactly as supplied by the project template.

### Host tools

`tools/host/` compiles the real `src/` game logic against small desktop stubs. It needs only `g++`.

```text
tools/host/run.sh             # soak: a bot pilot flies 6 seeds x 2 simulated hours
```

The soak fails on a stuck trip, a theater that never resolves, NaN positions, broken sheet ranges, or non-ASCII text reaching the display font. `docksweep.cpp` tests the docking computer from random approaches. `snap.cpp` + `render.py` render scripted frames of every state to PNG for a look check. `tests.sh` runs the focused tests: job completion, lanes and the atlas, saves across power cycles, and the deep encounters.

### Local build

```text
pio run -e m5stack-core2
```

### Web flasher

The Pages site contains the generated merged firmware and an `esp-web-tools` installer for compatible browsers.

## Places keep their lanes

Arriving somewhere for the first time deals it its own gates: mostly to places nobody has heard of, sometimes back to a name you have only seen. After that a place keeps the same lanes (and the same dock, or lack of one) until the escape pod launches. The place you came from always offers the gate back. A rumor opens a new lane from wherever you heard it. The map shows exactly this memory, and wipes with it; charted fixed points survive.

## Saving

With an SD card in the Core2, the game keeps `/spacemantis-saves/pilot.sav` (the whole game, including where you are and how far into a dive), a readable `journal.txt`, and a `README.txt`. It saves on meaningful events and every half minute, and on power-up picks up where you left it. Internal flash keeps a mirror, so no card means nothing is lost.

- **New game:** hold **A + C** on the title screen for two seconds. The bar turns red just before the save is cleared. Or delete `pilot.sav` (only that file) from the card.

## Below the roads

Something lives down there. It is rare, and it never hurts the ship.
