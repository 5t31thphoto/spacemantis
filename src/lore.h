#pragma once
#include "sheet_types.h"

namespace sm {

const char *careerPerkLine(CareerId id, uint8_t rank);
const char *stationMood(uint8_t stationType, uint8_t pressure, uint32_t seed);
const char *marketRumor(uint8_t pressure, uint32_t seed);
const char *deepWhisper(uint8_t band, uint32_t seed);
const char *lossLine(uint32_t lives, uint32_t seed);
const char *discoveryLine(uint8_t band, uint32_t seed);

} // namespace sm
