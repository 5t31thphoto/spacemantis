#pragma once
// ============================================================
//  SpaceMantis — the save file on the SD card
//  /spacemantis-saves/pilot.sav holds the whole game: pilot sheet, lead,
//  atlas, journal and a session block (where you are, mid-trip state).
//  Internal flash keeps a mirror, so a missing card never loses a pilot.
// ============================================================
#include <stdint.h>
#include <stddef.h>

namespace sm {

bool sdBegin();          // mount the card and make the folder; false = no card
bool sdReady();
bool sdHasSave();
bool sdManaged();        // the folder marker exists: the card has saved this game before
bool sdSaveGame(const void *session, uint16_t sessionLen);
// Restores sheet, lead, atlas and journal in memory; fills the session block.
bool sdLoadGame(void *session, uint16_t sessionLen);
bool sdDeleteSave();
void sdWriteJournalText();   // a human-readable journal.txt beside the save

}  // namespace sm
