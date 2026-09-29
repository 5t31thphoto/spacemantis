# SpaceMantis

**Roguelike space-career game for M5Stack Core2.**

SpaceMantis is an Elite-like game with the spreadsheet hidden underneath the flight view. There is no galaxy map, no plotted course, no hardpoint screen, and no inventory Tetris. The player flies into things and makes opportunistic decisions.

> **See what is here → commit by flying or one verb → dive or deal → progress ticks → die lost but not erased → until depth is the real map.**

## The rules

- **No course plotting.** Navigation is semantic gates plus subspace depth.
- **No ship-management minigame.** Earned capability is automatically equipped.
- **No quest log.** A station or encounter offers a small local hand of work; accepting it creates one active lead.
- **No galaxy map.** The player knows only what has recently been heard, what gates are visible, and what deep landmarks have become meaningful.
- **Dive, don't jump.** Three consecutive gate threads commit a subspace traversal. Resurfacing is a dangerous shortcut.
- **Lost in space is literal.** Destruction reseeds the universe and wipes surface/shallow place knowledge while the pilot, career and fitted capabilities survive. **In fiction the pilot only knows they are lost** — theories about the deep, bad jumps, charts that lie. They never learn that their previous cosmos is gone.
- **The deep becomes smaller.** Deep layers contain a rising fraction of unique and persistent places. Some are authored signatures; others are procedural places that acquire persistence when discovered.

## Flight

### Controls

- **Tilt:** steer relative to a captured neutral flight pose.
- **Touch drag:** yaw / roll attitude. It never drags the sky.
- **C:** re-center attitude (captures the current hand pose as neutral).
- **Reticle / alignment:** light assistance exists only when nearly threading a gate.
- **Gate threading:** a gate only counts when the ship is actually aligned with it. Missing a gate does not silently select it.
- **Contacts:** approach while you keep flying. Tap the left verb (HAIL / MINE / SCOOP / SALVAGE / READ / RESCUE / DOCK) or ATTACK, or press A / B. Ignoring a contact is a valid choice, but pirates, sub-pirates, hostiles and angry security may open fire when they get close. Cloak and Ghost rank help you slip them.

### Travel layers

| Layer | Feel | Encounters |
|---|---|---|
| Real space | restrained procedural stars and dusty nebulae | travelers, merchants, security, pirates, stations, asteroids, gas giants, escape pods, wrecks, artifacts |
| Shallow | mild distortion | lost travelers, sub-pirates, anomalies |
| Deep | stronger alien geometry | hostile entities, anomalies, rare landmarks |
| Deeper | increasingly unique | persistent/deep routes, signature places |
| Deepest | dense, strange, small | highest landmark density and authored share |

Each layer is one band, 1 (real) through 5 (deepest). A gate's `dN` label is the layer it opens toward. Three threaded gates create the actual dive/resurface transition, and the third gate decides where: a deeper gate takes you to its layer, a same-layer gate pushes one layer down, a shallower gate climbs to it, and **RESURFACE** (always present below real space) climbs straight to real space — a shortcut that costs extra glitch risk from far down and leaves a wake sub-pirates notice. Diving past your depth rating risks hull damage. A climb with an empty tank is paid for in hull, so nobody is stranded below. The player never chooses a coordinate from a menu.

## The three verbs

### HAIL — green

Parley, trade, bluff, request help, rescue, receive rumors, read anomalies, or interact with a landmark.

### ATTACK — red

Combat is a short resolve, not a dogfight simulator. Weapons, shields, career ranks, depth and encounter type alter the result.

### DOCK — blue

A station is an automatic approach: thread a DOCK gate or tap DOCK on a station contact (rare deep docks exist too). The board is intentionally thin and stays open until you undock:

| Row | What it does |
|---|---|
| Refuel / repair | full top-up, or a partial refill when credit is thin; a broke pilot with a dry tank gets fronted enough to reach a gas giant |
| Work | accept the board's job as your one active lead, or drop the current lead |
| Buy rumor | adds an unknown gate name to the local hand |
| Sell haul / buy gear | sells hold lines your lead doesn't own; with nothing to sell, offers one specific capability upgrade with its price |
| Board | the station's opportunity card, taken as your lead (once per dock) |
| Undock | back to the gates |

Tap a row to select it and tap it again to commit, or use **B** commit, **C** next, **A** undock.

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

A deep landmark is not truly persistent merely because it exists in the procedural table. The pilot has to **discover it**; discovery writes a persistent landmark flag. That is how the game can eventually reveal that some places are genuinely surviving from one lost universe to another.

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

## The long arc

Every persistent landmark you thread is written into the pilot sheet and survives universe wipes. Undiscovered landmarks only show up within one layer of where they live, so the deepest names have to be earned by going down. Milestones mark the way, and threading the last fixed point shows the one ending card — after which you keep flying.

## Leads

Only one job is ever active. The HUD shows its name, progress and what finishes it. Cargo leads point at a named gate (marked `JOB` in flight); bounty, rescue, survey, tour, escort, ghost, depth-run and landmark-watch leads finish through what you do rather than where you go. The active lead survives reboot; it does not survive losing the ship.

## Current content families

The current encounter deck includes travelers, merchants, security, surface pirates, sub-pirates, hostile deep contacts, anomalies, asteroids, gas giants, stations/outposts, escape pods, wrecks and deep artifacts. Wrecks and artifacts use the same one-commit interaction language as mining: aim, Hail/Salvage, resolve, continue flying.

The station board now includes haul, bounty, rescue, survey, smuggle, tourism, outpost supply, escort and market work. Work only becomes active when the player commits to it; merely looking at an offer has no cargo side effect.

## Repository / deployment

The project keeps the supplied Mantis GitHub/PlatformIO structure and the supplied Pages workflow. The workflow consumes a root project ZIP when present, replaces `src/` as a payload, builds `m5stack-core2`, stages the merged firmware, and deploys the web flasher to GitHub Pages.

The workflow directory is intentionally preserved exactly as supplied by the project template.

### Host soak test

`tools/host/run.sh` compiles the real `src/` logic against small desktop stubs and flies a bot pilot through several simulated hours per seed. It fails on softlocks (a combat, mining or dive that never finishes), broken sheet invariants, or non-ASCII text reaching the display font. It needs only `g++`.

```text
tools/host/run.sh        # 6 seeds x 2 simulated hours
```

### Local build

```text
pio run -e m5stack-core2
```

### Web flasher

The Pages site contains the generated merged firmware and an `esp-web-tools` installer for compatible browsers.
