#include "content.h"
#include "sheet.h"
#include <stdio.h>
#include <string.h>

namespace sm {
namespace {

static const char *A[] = {
  "Keld", "Vey", "Orr", "Myr", "Cind", "Nahl", "Pyr", "Sul",
  "Ash", "Rook", "Vel", "Jot", "Kest", "Dusk", "Brin", "Harr",
  "Morrow", "Sable", "Ilex", "Vanta", "Cael", "Rime", "Thorn", "Aster",
  "Mantis", "Gannet", "Hollow", "Pale", "Red", "Quiet", "Far", "Old",
  "Glass", "Black", "Blue", "Green", "White", "Long", "Last", "Second",
  "Under", "Still", "Broken", "Fallow", "Iron", "Soft", "Cold", "Bright",
  "Deep", "Near", "Outer", "Inner", "Waking", "Sleeping", "Lost", "Found",
  "North", "South", "East", "West", "False", "True", "Once", "Again"
};
static const char *B[] = {
  "Reach", "Well", "Gate", "Shelf", "Drift", "Hollow", "Span", "Mere",
  "Road", "Fall", "Verge", "Crown", "Pit", "Sound", "March", "Cross",
  "Station", "Fold", "Wake", "Shelf", "Arc", "Ring", "Cut", "Field",
  "Water", "Market", "Glass", "Line", "Mouth", "Turn", "Step", "Trace",
  "Bell", "Lantern", "Keel", "Harbor", "Pocket", "Window", "Scar", "Well",
  "Below", "Memory", "End", "Beginning", "Door", "Roadstead", "Anchor", "Current",
  "Garden", "Cemetery", "Library", "Mile", "Signal", "Country", "House", "City",
  "Point", "Vale", "Basin", "Ruin", "Haven", "Outside", "Inside", "Center"
};

static const char *DEEP[] = {
  "The Still Road", "Mantis Glass", "Under-Customs", "Black Verge",
  "Second Keel", "The Quiet Mouth", "Lantern Below", "The Narrow Country",
  "Old Gravity", "The Unlit Market", "The Long Return", "The Small End",
  "Crown of Nothing", "The Remembered Gate", "The Deep Station",
  "The Place That Waits", "The Last Ordinary Star", "The Wrong Harbor",
  "A Road Under Roads", "The Name Beneath Your Name", "The Patient Fold",
  "The Black Orchard", "The Soft Boundary", "The Other Surface"
};

static const char *CONTACT[] = {
  "someone is already here", "a light blinks twice, then once", "the contact drifts like it has nowhere to go",
  "they are carrying more fuel than a sane pilot would", "the ship's silhouette is almost familiar",
  "their transponder is clean in a suspicious way", "they answer on an old channel",
  "the pilot asks if you know the way back", "their navigation lights are hand-painted",
  "they have a station name but no coordinates", "they call the gate by a name you have never heard",
  "they seem relieved to see another ship", "they have been waiting beside the landmark",
  "they say the deep is smaller than the surface", "they ask what year your ship thinks it is",
  "they have a cargo manifest with a blank destination", "they insist the portal was behind you",
  "they offer a rumor without asking for payment", "they say your ship looks different than last time",
  "they ask whether you remember a place you cannot name"
};

static const char *REAL_STORY[] = {
  "A station clerk calls you by a name that belongs to another pilot.",
  "A merchant sells a rumor and refuses to explain why it matters.",
  "A patrol is more interested in your gate label than your cargo.",
  "Someone has painted a route marker on an asteroid. It points nowhere.",
  "A traveler asks if you have seen a blue gate with no station behind it.",
  "A fuel seller says the price is high because three ships arrived from the same direction.",
  "A dockhand says the universe has been repeating lately. Nobody laughs.",
  "You hear a familiar name from a radio channel you have never used.",
  "A pilot swears they just came from a place you have never heard of.",
  "A cargo broker offers twice the usual price for something ordinary."
};
static const char *SHALLOW_STORY[] = {
  "A lost ship follows your wake until you reach the next gate.",
  "A sub-pirate asks what you saw on the surface. It does not seem to mean space.",
  "The portal closes behind you before the other ship finishes speaking.",
  "Something has left navigation lights hanging in empty subspace.",
  "A traveler says the under has currents. Your instruments disagree.",
  "A merchant is carrying cargo that vibrates whenever you approach a gate.",
  "You see a station through a distortion, but it is not there when you turn.",
  "A distress call repeats your own last transmission."
};
static const char *DEEP_STORY[] = {
  "The stars have stopped pretending they are stars.",
  "A hostile contact knows the name of a gate you only heard once.",
  "Your scanner draws a map for half a second, then forgets it.",
  "A persistent landmark appears where no procedural name should survive.",
  "The same station name appears on two unrelated gates.",
  "A merchant offers a route that would be impossible on the surface.",
  "You pass a light source that casts a shadow toward the ship.",
  "The portal ahead looks older than the universe around it.",
  "A radio voice tells you that deeper space is smaller."
};
static const char *ABYSS_STORY[] = {
  "The deep has become a neighborhood.",
  "You recognize a landmark from a life that ended somewhere else.",
  "A gate carries a name that survived the wipe.",
  "The route is short because there is almost nowhere left between here and there.",
  "A station has kept the same light through more than one universe.",
  "Something asks whether you are finally ready to stop being lost.",
  "You see a place that feels handmade even though nobody built it.",
  "The deep does not look infinite anymore. That is somehow worse.",
  "A gate opens onto a familiar road with no coordinates attached."
};

static const StoryBeat BEATS[] = {
  {"DRIFT", "A station clerk calls you by a name that belongs to another pilot.", 0, 8},
  {"RUMOR", "A merchant sells a rumor and refuses to explain why it matters.", 0, 8},
  {"PATROL", "A patrol is more interested in your gate label than your cargo.", 0, 7},
  {"MARKER", "Someone has painted a route marker on an asteroid. It points nowhere.", 0, 7},
  {"LOST", "A traveler asks if you have seen a blue gate with no station behind it.", 0, 9},
  {"WAKE", "A lost ship follows your wake until you reach the next gate.", 1, 9},
  {"BREACH", "A sub-pirate asks what you saw on the surface. It does not seem to mean space.", 1, 8},
  {"ECHO", "A distress call repeats your own last transmission.", 1, 7},
  {"FOLD", "You see a station through a distortion, but it is not there when you turn.", 1, 8},
  {"WRONG STAR", "The stars have stopped pretending they are stars.", 2, 9},
  {"MEMORY", "Your scanner draws a map for half a second, then forgets it.", 2, 9},
  {"LANDMARK", "A persistent landmark appears where no procedural name should survive.", 2, 10},
  {"DUPLICATE", "The same station name appears on two unrelated gates.", 2, 7},
  {"SHADOW", "You pass a light source that casts a shadow toward the ship.", 3, 8},
  {"OLD PORTAL", "The portal ahead looks older than the universe around it.", 3, 9},
  {"SMALL WORLD", "A radio voice tells you that deeper space is smaller.", 3, 10},
  {"NEIGHBORHOOD", "The deep has become a neighborhood.", 4, 10},
  {"RECOGNITION", "You recognize a landmark from a life that ended somewhere else.", 4, 10},
  {"SURVIVOR", "A gate carries a name that survived the wipe.", 4, 10},
  {"CLOSE", "The deep does not look infinite anymore. That is somehow worse.", 4, 10}
};

static char s_name[NAME_LEN];
static char s_line[96];

} // namespace

const char *placeName(uint32_t seed, uint8_t band, bool deep) {
  if (deep && band >= DEPTH_ABYSS) return deepName(seed);
  snprintf(s_name, sizeof(s_name), "%s %s", A[seed & 63], B[(seed >> 6) & 63]);
  return s_name;
}

const char *deepName(uint32_t seed) {
  return DEEP[seed % (sizeof(DEEP) / sizeof(DEEP[0]))];
}

const char *contactLine(EncounterClass c, uint8_t band, uint32_t seed) {
  const char *base = CONTACT[(seed + c * 7 + band * 11) % (sizeof(CONTACT) / sizeof(CONTACT[0]))];
  snprintf(s_line, sizeof(s_line), "%s; %s.", base,
           (band >= DEPTH_DEEP) ? "the deep feels close" : "the sky feels very large");
  return s_line;
}

const char *landmarkLine(const Landmark &lm, uint32_t seed) {
  static const char *AUX[] = {
    "its light never changes", "its gate keeps one old label", "the scanners refuse to measure it",
    "ships leave it alone", "someone maintains a dock light", "the place survives every story",
    "its geometry is almost ordinary", "it feels deliberately placed", "it is older than its route",
    "it is quiet enough to hear the hull"
  };
  snprintf(s_line, sizeof(s_line), "%s — %s.", lm.name, AUX[(seed + lm.id) % 10]);
  return s_line;
}

const StoryBeat &storyBeat(uint8_t band, uint32_t seed) {
  // Choose from all beats at or below the current depth. Higher depth increases
  // the probability of choosing a high-depth beat rather than replacing lower ones.
  static StoryBeat fallback = {"DRIFT", "The universe is quiet around the ship.", 0, 1};
  uint16_t weight = 0;
  for (const StoryBeat &b : BEATS) if (b.minBand <= band) weight += b.weight;
  if (!weight) return fallback;
  uint16_t pick = (uint16_t)(seed % weight);
  for (const StoryBeat &b : BEATS) {
    if (b.minBand > band) continue;
    if (pick < b.weight) return b;
    pick = (uint16_t)(pick - b.weight);
  }
  return fallback;
}

} // namespace sm
