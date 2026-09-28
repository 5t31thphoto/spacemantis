#include "lore.h"
#include <stdio.h>

namespace sm {
namespace {
static const char *CAREER[CR_COUNT][8] = {
  {
    "The hold is still mostly empty.", "You learn what bulk costs.", "Stations trust your manifests a little more.",
    "Cargo starts fitting where cargo should not.", "Supply routes stop feeling accidental.", "You can make a living from being between places.",
    "Large jobs stop looking large.", "The ship feels like a warehouse with engines."
  },
  {
    "A weapon is better than no weapon.", "You learn when not to fire.", "Pirates start recognizing the silhouette.",
    "Your first shot carries an expectation.", "Bounties become ordinary work.", "Threats sometimes choose to leave.",
    "The ship's resolve favors you more often.", "You have become part of the threat weather."
  },
  {
    "Ore is everywhere if you look.", "You learn which rocks lie.", "The cutters stop wasting themselves.",
    "Deep ore starts paying for deep fuel.", "Strange matter becomes a commodity.", "Some landmarks look geological until they answer.",
    "You can read a field by its silence.", "The rock sometimes knows your name."
  },
  {
    "You learn to answer distress.", "People remember a pilot who stops.", "Rescue work opens unusual doors.",
    "Your shields buy time for someone else.", "Escorts start seeking you out.", "You can cross trouble without abandoning the weak.",
    "A rescue can become a story.", "The lost sometimes recognize you first."
  },
  {
    "You notice price weather.", "You stop buying at the obvious station.", "Rumors become part of the cargo.",
    "The same crate means different things in different places.", "Stations begin offering you better work.", "You can smell a market before the numbers move.",
    "Commerce becomes a way to explore.", "You trade in routes as much as goods."
  },
  {
    "Unknown gates are not mistakes.", "You learn to remember names without maps.", "The universe starts repeating its grammar.",
    "A blank gate becomes an invitation.", "Scanners reveal what eyes cannot.", "You stop needing to know where you are.",
    "You start wondering how deep you can go.", "The unknown feels more useful than the known."
  },
  {
    "The first deep gate hurts.", "Bulkheads turn courage into a number.", "You learn to read glitch weather.",
    "Shallow space stops feeling like the destination.", "The deep begins to rhyme.", "You find names that should not persist.",
    "The deep is becoming smaller.", "You are no longer lost in the same way."
  },
  {
    "Cloak buys seconds.", "Security notices less.", "Heat can be allowed to cool.",
    "Quiet routes become a career.", "Some cargo is easier to hide than explain.", "The under still notices a breach.",
    "You know which questions not to answer.", "The ship passes through stories without becoming one."
  }
};

static const char *STATIONS[][8] = {
  {
    "the dock is busy", "the clerk is bored", "someone is selling fuel by hand", "a repair crew is sleeping",
    "three merchants arrived together", "the station lights flicker", "the board is crowded", "nobody asks where you came from"
  },
  {
    "the outpost is running thin", "a pump coughs between refuels", "the dockmaster wants machine parts", "repair prices are ugly",
    "a traveler offers water", "the security light is new", "the station has one good berth", "everyone is waiting for a supply ship"
  },
  {
    "the station is built around a gate", "the walls carry old route marks", "nobody mentions the deep", "the market has strange hours",
    "the dock beacon is older than the station", "a merchant watches every gate", "the board is mostly rumors", "the fuel tastes expensive"
  },
  {
    "the station feels temporary", "half the lights are emergency lights", "a pirate is pretending to be a customer", "the clerk asks about your cloak",
    "someone is selling contraband openly", "a patrol just left", "the repair bay is armed", "the station wants you moving"
  },
  {
    "the station has no obvious owner", "the dock light is impossibly old", "the same name appears twice", "the board contains deep work",
    "a gate hums behind the market", "a clerk remembers another universe", "nobody seems surprised by your ship", "the station is too quiet"
  }
};

static const char *MARKETS[] = {
  "fuel prices twitch upward", "ore is suddenly ordinary", "scrap is worth more than it looks",
  "machine parts are scarce", "a sealed cargo premium is forming", "water is moving fast",
  "spice buyers are nervous", "the local house is hoarding repairs", "traders are buying routes",
  "the market wants something deep", "the station is flush with cargo", "everyone is waiting for a ship"
};

static const char *WHISPERS[] = {
  "the under has currents", "three gates make a road", "the deep remembers names",
  "the surface repeats itself", "some places survive", "a gate can be older than its destination",
  "the smallest routes are below", "the abyss is not infinite", "someone has been here before you",
  "the same place can have different weather", "a persistent landmark is not necessarily handmade",
  "the deep is dense with shortcuts", "the wrong portal can still be useful", "a ship can become a landmark",
  "the under notices resurfacing", "a cloak does not hide a breach", "the farthest place can be nearby",
  "unknown does not mean unreachable", "lost is a local condition", "mastery changes what lost means"
};

static const char *LOSS[] = {
  // Pilot-facing only: they believe they are lost after destruction / the deep.
  // They do not know a universe was reseeded.
  "Hull gone. Wake in a strange sky.",
  "Too deep. The charts don't match anymore.",
  "Some say the deep folds you into another lane.",
  "Nav is blank. Maybe the jump never finished.",
  "You remember names. None of the gates agree.",
  "Rescue never came. Only this empty heading.",
  "Theory: glitch dropped you half a sector off.",
  "Theory: the deep ate the road home.",
  "Same ship. Different stars. Keep flying.",
  "Lost again. The deep always collects a toll."
};

static const char *DISCOVER[] = {
  "The name sticks.", "The scanner cannot make it ordinary.", "This place refuses to be forgotten.",
  "The gate has become a landmark.", "Something about this place persists.", "The deep just gave you a fixed point.",
  "You have found one of the places that survives.", "This is no longer merely a procedural name.",
  "The route feels true in a way others do not.", "A fixed point in a sky that lies."
};

static char buf[112];
}

const char *careerPerkLine(CareerId id, uint8_t rank) {
  if (id >= CR_COUNT) return "The sheet is quiet.";
  uint8_t idx = rank > 7 ? 7 : rank;
  return CAREER[id][idx];
}

const char *stationMood(uint8_t stationType, uint8_t pressure, uint32_t seed) {
  uint8_t t = stationType % 5;
  uint8_t p = (uint8_t)((pressure / 15 + seed) % 8);
  snprintf(buf, sizeof(buf), "%s; %s.", STATIONS[t][p],
           pressure > 65 ? "security weather is heavy" : pressure > 35 ? "heat is noticeable" : "the dock feels loose");
  return buf;
}

const char *marketRumor(uint8_t pressure, uint32_t seed) {
  const char *m = MARKETS[(seed + pressure * 3) % (sizeof(MARKETS) / sizeof(MARKETS[0]))];
  snprintf(buf, sizeof(buf), "%s; %s.", m, pressure > 55 ? "buyers are paying to avoid questions" : "the spread is still moving");
  return buf;
}

const char *deepWhisper(uint8_t band, uint32_t seed) {
  const char *w = WHISPERS[(seed + band * 13) % (sizeof(WHISPERS) / sizeof(WHISPERS[0]))];
  snprintf(buf, sizeof(buf), "%s. %s.", w, band >= DEPTH_ABYSS ? "It feels close now" : "It is only a rumor");
  return buf;
}

const char *lossLine(uint32_t lives, uint32_t seed) {
  snprintf(buf, sizeof(buf), "%s %lu.", LOSS[seed % (sizeof(LOSS) / sizeof(LOSS[0]))], (unsigned long)lives);
  return buf;
}

const char *discoveryLine(uint8_t band, uint32_t seed) {
  snprintf(buf, sizeof(buf), "%s Depth %u.", DISCOVER[seed % (sizeof(DISCOVER) / sizeof(DISCOVER[0]))], band);
  return buf;
}

} // namespace sm
