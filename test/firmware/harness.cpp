#include "DaisyDuino.h"
#include "WeaselEditor.h"
#include <cstdio>
#include <string>
FakeUsbSerial SerialUSB;
static uint8_t qspiMem[8 * 1024 * 1024];
namespace daisy {
QSPIHandle::Result QSPIHandle::Init(const Config &) { return OK; }
void *QSPIHandle::GetData(uint32_t off) { return qspiMem + off; }
QSPIHandle::Result QSPIHandle::Erase(uint32_t a, uint32_t b) { uint32_t s = a & ~0xFFFu, e = (b + 0xFFF) & ~0xFFFu; memset(qspiMem + s, 0xFF, e - s); return OK; }
QSPIHandle::Result QSPIHandle::Write(uint32_t a, uint32_t n, uint8_t *buf) { for (uint32_t i = 0; i < n; i++) qspiMem[a + i] &= buf[i]; return OK; }
System::MemoryRegion System::GetProgramMemoryRegion() { return MemoryRegion::INTERNAL_FLASH; }
}
extern "C" void dsy_dma_invalidate_cache_for_buffer(int32_t *, size_t) {}
int boots = 0;
static uint8_t panelState[weasel::PANEL_COUNT] = {0,0,0,0,0,0,0,0,2,0,0,2,0,0};
static void rdPanel(uint8_t *o) { memcpy(o, panelState, weasel::PANEL_COUNT); }
static void wrPanel(const uint8_t *i) { memcpy(panelState, i, weasel::PANEL_COUNT); }
static void hook(weasel::Editor *e) { e->onBootloader = []() { boots++; }; e->readPanel = rdPanel; e->writePanel = wrPanel; }
int main() {
  memset(qspiMem, 0xFF, sizeof qspiMem);
  // two "power cycles": settings must survive in qspiMem
  char line[4096];
  weasel::Editor *ed = new weasel::Editor();
  hook(ed);
  ed->begin();
  while (fgets(line, sizeof line, stdin)) {
    std::string l(line);
    if (l.rfind("PRESS", 0) == 0) { panelState[0] = (panelState[0] + 1) % 3; printf("pressed\n"); fflush(stdout); continue; }
    if (l.rfind("REBOOT", 0) == 0) { delete ed; ed = new weasel::Editor(); hook(ed); ed->begin(); printf("rebooted\n"); fflush(stdout); continue; }
    if (l.rfind("NOTE", 0) == 0) { int c, n, v; sscanf(line + 5, "%d %d %d", &c, &n, &v); ed->noteIn(c, n, v); printf("ok accepts=%d\n", ed->acceptsChannel(c)); fflush(stdout); continue; }
    SerialUSB.in.clear(); SerialUSB.pos = 0; SerialUSB.out.clear();
    unsigned v; const char *p = line;
    int k; while (sscanf(p, "%x%n", &v, &k) == 1) { SerialUSB.in.push_back((uint8_t)v); p += k; }
    ed->poll();
    for (uint8_t b : SerialUSB.out) printf("%02x ", b);
    printf("| boots=%d\n", boots); fflush(stdout);
  }
}
