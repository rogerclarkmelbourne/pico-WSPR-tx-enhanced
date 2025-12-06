#ifndef PERSISTENT_STORAGE_H_
#define PERSISTENT_STORAGE_H_
#include <stdint.h>
#include "hardware/flash.h"
#include "hardware/sync.h"


const uint64_t  MAGIC_NUMBER    = 0x5069636F57535052;// 'PicoWSPR  
const uint32_t  CURRENT_VERSION = 0x02;

typedef struct  {
    uint64_t    magicNumber;
    uint32_t    settingsVersion;
    uint8_t     callsign[16];
    uint8_t     locator4[16];
    int8_t      bandIndex;
    int8_t      slotSkip;
} SettingsData;

SettingsData settingsData;

// Use last sector at the top of flash for storage
const uint32_t FLASH_TARGET_OFFSET = (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

extern void settingsReadFromFlash(void);
extern void settingsWriteToFlash(void);
extern bool settingsCheckSettings(void);


#endif