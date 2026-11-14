// Game resets for the Vita (see vita_reset.h).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vita_reset.h"

#ifdef _WIN32
// PE has no __start_/__stop_ symbols for custom sections: find "gamedata" and "gamebss" (renamed
// in CMakeLists.txt) in the section table of the running image instead.
#include <windows.h>
extern IMAGE_DOS_HEADER __ImageBase;

static void WinFindSection(const char *name, char **start, char **stop)
{
    const char *base = (const char *)&__ImageBase;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + __ImageBase.e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (strncmp((const char *)sec[i].Name, name, 8) == 0)
        {
            *start = (char *)base + sec[i].VirtualAddress;
            *stop = *start + sec[i].Misc.VirtualSize;
            return;
        }
    }
    *start = *stop = NULL;
}

static char *sGameDataStart, *sGameDataStop, *sGameBssStart, *sGameBssStop;
static void WinInit(void)
{
    if (sGameDataStart == NULL)
    {
        WinFindSection("gamedata", &sGameDataStart, &sGameDataStop);
        WinFindSection("gamebss", &sGameBssStart, &sGameBssStop);
    }
}
#define __start_gamedata (WinInit(), sGameDataStart)
#define __stop_gamedata (WinInit(), sGameDataStop)
#define __start_gamebss (WinInit(), sGameBssStart)
#define __stop_gamebss (WinInit(), sGameBssStop)
#else
extern char __start_gamedata[], __stop_gamedata[];
extern char __start_gamebss[], __stop_gamebss[];
#endif

static char *sBootSnapshot;

static uint32_t DataSize(void) { return (uint32_t)(__stop_gamedata - __start_gamedata); }
static uint32_t BssSize(void)  { return (uint32_t)(__stop_gamebss - __start_gamebss); }

void VitaReset_TakeBootSnapshot(void)
{
    if (sBootSnapshot == NULL)
        sBootSnapshot = malloc(DataSize() + BssSize());
    if (sBootSnapshot != NULL)
    {
        memcpy(sBootSnapshot, __start_gamedata, DataSize());
        memcpy(sBootSnapshot + DataSize(), __start_gamebss, BssSize());
    }
}

bool VitaReset_RestoreBootSnapshot(void)
{
    if (sBootSnapshot == NULL)
        return false;
    memcpy(__start_gamedata, sBootSnapshot, DataSize());
    memcpy(__start_gamebss, sBootSnapshot + DataSize(), BssSize());
    return true;
}
