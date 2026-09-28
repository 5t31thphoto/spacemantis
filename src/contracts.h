#pragma once
// Opportunistic work — not a quest log. Active lead only.
#include "sheet_types.h"
#include "universe.h"

namespace sm {

enum ContractKind : uint8_t {
  CK_NONE = 0,
  CK_HAUL,          // deliver bulk somewhere (semantic)
  CK_BOUNTY,        // attack a marked contact class
  CK_RESCUE,        // hail a distress
  CK_SURVEY,        // thread unknown gates / deep band
  CK_SMUGGLE,       // hot cargo
  CK_TOUR,          // passenger / tourism
  CK_SUPPLY,        // station/outpost supply
  CK_ESCORT,        // protect a weak contact
  CK_MARKET,        // speculative legal cargo
  CK_COUNT
};

struct Contract {
  ContractKind kind;
  char title[28];
  char dest[NAME_LEN];     // gate label to chase (may be empty = opportunistic)
  uint8_t destDepth;
  int16_t pay;
  uint16_t xp;
  CareerId track;
  uint8_t progress;        // 0..need
  uint8_t need;
  uint8_t live;            // 1 active
};

void contractsInit();
Contract &contract();
bool contractOffer(ContractKind prefer = CK_NONE);  // roll a new offer onto station board
void contractAccept(const Contract &offer);
void contractAbandon();
void contractOnGate(const char *label, uint8_t band, bool unknown);
void contractOnResolve(EncounterClass who, bool attacked, bool destroyedOther);
void contractOnMine();
void contractTick();
Contract &contractOfferPeek();  // last rolled offer (station board)

}  // namespace sm
