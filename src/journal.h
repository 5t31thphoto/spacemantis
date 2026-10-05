#pragma once
// ============================================================
//  SpaceMantis — the pilot's journal
//  Short entries in the pilot's own voice. It rides in the escape pod, so it
//  survives every lost universe; each entry remembers which life wrote it.
// ============================================================
#include <stdint.h>

namespace sm {

static const int JOURNAL_ENTRIES = 24;
static const int JOURNAL_TEXT = 72;

struct JournalEntry { uint16_t life; char text[JOURNAL_TEXT]; };
struct Journal {
  uint8_t version, count, head;   // ring buffer: head is the next slot to write
  uint8_t pad;
  JournalEntry e[JOURNAL_ENTRIES];
};

Journal &journal();
void journalClear();
void journalAdd(const char *text);
const JournalEntry *journalNewest(int k);   // k = 0 newest; nullptr past the end
bool journalSave();
bool journalLoad();

}  // namespace sm
