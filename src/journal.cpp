#include "journal.h"
#include "sheet.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace sm {
namespace { Journal s_j{}; const uint8_t JOURNAL_VERSION = 1; }

Journal &journal() { return s_j; }
void journalClear() { memset(&s_j, 0, sizeof(s_j)); s_j.version = JOURNAL_VERSION; }

void journalAdd(const char *text) {
  if (!text || !text[0]) return;
  // never write the same line twice in a row
  if (const JournalEntry *last = journalNewest(0)) if (strncmp(last->text, text, JOURNAL_TEXT - 1) == 0) return;
  JournalEntry &e = s_j.e[s_j.head];
  e.life = (uint16_t)(sheet().lives + 1);
  // keep the font happy: ASCII only
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p && o + 1 < JOURNAL_TEXT; ) {
    if (*p < 0x80) { e.text[o++] = (char)*p++; continue; }
    int n = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    for (int k = 0; k < n && *p; k++) p++;
    e.text[o++] = '-';
  }
  e.text[o] = 0;
  s_j.head = (uint8_t)((s_j.head + 1) % JOURNAL_ENTRIES);
  if (s_j.count < JOURNAL_ENTRIES) s_j.count++;
}

const JournalEntry *journalNewest(int k) {
  if (k < 0 || k >= s_j.count) return nullptr;
  int idx = ((int)s_j.head - 1 - k + JOURNAL_ENTRIES * 2) % JOURNAL_ENTRIES;
  return &s_j.e[idx];
}

bool journalSave() {
  Preferences prefs;
  if (!prefs.begin("sm_journal", false)) return false;
  s_j.version = JOURNAL_VERSION;
  prefs.putBytes("log", &s_j, sizeof(s_j));
  prefs.end();
  return true;
}

bool journalLoad() {
  Preferences prefs;
  journalClear();
  if (!prefs.begin("sm_journal", true)) return false;
  bool ok = false;
  if (prefs.getBytesLength("log") == sizeof(s_j)) {
    Journal j; prefs.getBytes("log", &j, sizeof(j));
    if (j.version == JOURNAL_VERSION && j.count <= JOURNAL_ENTRIES && j.head < JOURNAL_ENTRIES) { s_j = j; ok = true; }
  }
  prefs.end();
  return ok;
}

}  // namespace sm
