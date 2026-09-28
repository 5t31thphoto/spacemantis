#pragma once
#include "universe.h"

namespace sm {

struct StoryBeat {
  const char *title;
  const char *text;
  uint8_t minBand;
  uint8_t weight;
};

// Content is deliberately card-sized: enough to imply a huge universe without
// building a giant dialogue engine or quest log.
const char *placeName(uint32_t seed, uint8_t band, bool deep);
const char *deepName(uint32_t seed);
const char *contactLine(EncounterClass c, uint8_t band, uint32_t seed);
const char *landmarkLine(const Landmark &lm, uint32_t seed);
const StoryBeat &storyBeat(uint8_t band, uint32_t seed);

} // namespace sm
