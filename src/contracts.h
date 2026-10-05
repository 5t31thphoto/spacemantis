#pragma once
// Opportunistic work — not a quest log. Active lead only.
#include "sheet_types.h"
#include "universe.h"

namespace sm {

struct Opportunity;  // sim.h

enum ContractKind : uint8_t {
  CK_NONE = 0,
  CK_HAUL,          // deliver bulk somewhere (semantic)
  CK_BOUNTY,        // attack a marked contact class
  CK_RESCUE,        // hail a distress
  CK_SURVEY,        // go somewhere you have never heard of
  CK_SMUGGLE,       // hot cargo
  CK_TOUR,          // passenger / tourism
  CK_SUPPLY,        // station/outpost supply
  CK_ESCORT,        // protect a weak contact
  CK_MARKET,        // speculative legal cargo
  CK_GHOST,         // pass a security hail clean
  CK_DEPTHRUN,      // reach a deep-rated place
  CK_LANDMARK,      // chart a persistent place
  CK_COUNT
};

struct Contract {
  ContractKind kind;
  char title[28];
  char dest[NAME_LEN];     // place to travel to (empty = opportunistic)
  uint8_t destDepth;       // how deep the dive to get there goes (1..4)
  int16_t pay;
  uint16_t xp;
  CareerId track;
  uint8_t progress;        // 0..need
  uint8_t need;
  uint8_t live;            // 1 active
};

void contractsInit();
void contractSetHere(const char *place);   // current location: jobs never point here
Contract &contract();
bool contractOffer(ContractKind prefer = CK_NONE);  // roll a new offer onto station board
bool contractAccept(const Contract &offer);          // false if hold or purse refuses
bool contractFromOpportunity(const Opportunity &op); // a board card becomes the lead
void contractAbandon();
// label = place arrived at ("" for a gate on the way); unknown = a place never heard of
void contractOnGate(const char *label, uint8_t band, bool unknown);
void contractOnResolve(EncounterClass who, bool attacked, bool destroyedOther);
void contractOnMine();
void contractOnLandmark();
void contractTick();
Contract &contractOfferPeek();
const char *contractCargo(ContractKind k);      // hold line owned by a cargo job, or nullptr
const char *contractHint(const Contract &c);    // one short line: what finishes it
bool contractTakeCompleted(Contract &out);      // true once after a lead completes

bool contractsSave();
bool contractsLoad();

}  // namespace sm
