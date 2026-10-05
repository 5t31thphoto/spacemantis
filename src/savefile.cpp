#include "savefile.h"
#include "sheet.h"
#include "contracts.h"
#include "atlas.h"
#include "journal.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>

namespace sm {
namespace {
const char *DIR = "/spacemantis-saves";
const char *SAVE = "/spacemantis-saves/pilot.sav";
const char *TMP = "/spacemantis-saves/pilot.tmp";
const char *MARK = "/spacemantis-saves/README.txt";
const char *JTXT = "/spacemantis-saves/journal.txt";
const uint32_t MAGIC = 0x56534D53u;   // "SMSV"
const uint16_t VERSION = 1;
bool s_ready = false;

struct Header {
  uint32_t magic;
  uint16_t version, pilotLen, contractLen, atlasLen, journalLen, sessionLen;
  uint32_t checksum;
};

uint32_t fnv(uint32_t h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
  return h;
}

uint32_t payloadSum(const void *session, uint16_t sl) {
  uint32_t h = 2166136261u;
  h = fnv(h, &sheet(), sizeof(Pilot));
  h = fnv(h, &contract(), sizeof(Contract));
  h = fnv(h, &atlas(), sizeof(Atlas));
  h = fnv(h, &journal(), sizeof(Journal));
  return fnv(h, session, sl);
}
}  // namespace

bool sdBegin() {
  // Core2: the card shares the display's SPI bus; chip select is GPIO 4
  s_ready = SD.begin(GPIO_NUM_4, SPI, 25000000);
  if (!s_ready) return false;
  if (!SD.exists(DIR)) SD.mkdir(DIR);
  return true;
}

bool sdReady() { return s_ready; }
bool sdHasSave() { return s_ready && SD.exists(SAVE); }
bool sdManaged() { return s_ready && SD.exists(MARK); }

bool sdSaveGame(const void *session, uint16_t sessionLen) {
  if (!s_ready) return false;
  Header h{MAGIC, VERSION, (uint16_t)sizeof(Pilot), (uint16_t)sizeof(Contract), (uint16_t)sizeof(Atlas),
           (uint16_t)sizeof(Journal), sessionLen, payloadSum(session, sessionLen)};
  File f = SD.open(TMP, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write((const uint8_t *)&h, sizeof(h));
  w += f.write((const uint8_t *)&sheet(), sizeof(Pilot));
  w += f.write((const uint8_t *)&contract(), sizeof(Contract));
  w += f.write((const uint8_t *)&atlas(), sizeof(Atlas));
  w += f.write((const uint8_t *)&journal(), sizeof(Journal));
  w += f.write((const uint8_t *)session, sessionLen);
  f.close();
  size_t want = sizeof(h) + sizeof(Pilot) + sizeof(Contract) + sizeof(Atlas) + sizeof(Journal) + sessionLen;
  if (w != want) { SD.remove(TMP); return false; }
  // swap the finished file into place so a power cut never leaves half a save
  if (SD.exists(SAVE)) SD.remove(SAVE);
  if (!SD.rename(TMP, SAVE)) return false;
  if (!SD.exists(MARK)) {
    File m = SD.open(MARK, FILE_WRITE);
    if (m) {
      const char *txt = "SpaceMantis saves\r\n\r\npilot.sav   the whole game. Delete it (only it) to start a new game.\r\n"
                        "journal.txt the pilot's journal, newest first.\r\n\r\nOr hold A + C on the title screen for two seconds.\r\n";
      m.write((const uint8_t *)txt, strlen(txt));
      m.close();
    }
  }
  return true;
}

bool sdLoadGame(void *session, uint16_t sessionLen) {
  if (!s_ready || !SD.exists(SAVE)) return false;
  File f = SD.open(SAVE, FILE_READ);
  if (!f) return false;
  Header h{};
  bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == MAGIC && h.version == VERSION &&
            h.pilotLen == sizeof(Pilot) && h.contractLen == sizeof(Contract) && h.atlasLen == sizeof(Atlas) &&
            h.journalLen == sizeof(Journal) && h.sessionLen == sessionLen;
  if (!ok) { f.close(); return false; }
  // read into scratch first; only commit if the checksum agrees
  Pilot *p = new Pilot; Contract *c = new Contract; Atlas *a = new Atlas; Journal *j = new Journal;
  uint8_t *ses = new uint8_t[sessionLen];
  ok = f.read((uint8_t *)p, sizeof(Pilot)) == sizeof(Pilot) && f.read((uint8_t *)c, sizeof(Contract)) == sizeof(Contract) &&
       f.read((uint8_t *)a, sizeof(Atlas)) == sizeof(Atlas) && f.read((uint8_t *)j, sizeof(Journal)) == sizeof(Journal) &&
       f.read(ses, sessionLen) == sessionLen;
  f.close();
  if (ok) {
    uint32_t hsum = 2166136261u;
    hsum = fnv(hsum, p, sizeof(Pilot)); hsum = fnv(hsum, c, sizeof(Contract)); hsum = fnv(hsum, a, sizeof(Atlas));
    hsum = fnv(hsum, j, sizeof(Journal)); hsum = fnv(hsum, ses, sessionLen);
    ok = hsum == h.checksum;
  }
  if (ok) {
    sheet() = *p; contract() = *c; atlas() = *a; journal() = *j;
    memcpy(session, ses, sessionLen);
  }
  delete p; delete c; delete a; delete j; delete[] ses;
  return ok;
}

bool sdDeleteSave() {
  if (!s_ready) return false;
  if (SD.exists(SAVE)) SD.remove(SAVE);
  if (SD.exists(TMP)) SD.remove(TMP);
  return !SD.exists(SAVE);
}

void sdWriteJournalText() {
  if (!s_ready) return;
  File f = SD.open(JTXT, FILE_WRITE);
  if (!f) return;
  const char *head = "SPACEMANTIS - PILOT JOURNAL (newest first)\r\n\r\n";
  f.write((const uint8_t *)head, strlen(head));
  char line[96];
  for (int k = 0; ; k++) {
    const JournalEntry *e = journalNewest(k);
    if (!e) break;
    int n = snprintf(line, sizeof(line), "life %u  %s\r\n", e->life, e->text);
    if (n > 0) f.write((const uint8_t *)line, (size_t)n);
  }
  f.close();
}

}  // namespace sm
