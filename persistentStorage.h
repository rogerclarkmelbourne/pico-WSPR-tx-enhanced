#ifndef PERSISTENT_STORAGE_H_
#define PERSISTENT_STORAGE_H_

#include <stdint.h>
#include <ctype.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

#define MAX_KEY 64
#define MAX_VAL 256
#define NUM_BANDS 9

extern const uint64_t  MAGIC_NUMBER ;
extern const uint32_t  CURRENT_VERSION;

typedef struct  {
    uint64_t    magicNumber;
    uint32_t    settingsVersion;
    int32_t     freqCalibrationPPM;
    uint8_t     callsign[16];
    uint8_t     locator4[16];
    int32_t     bandsBitPattern;
    uint8_t     slotSkip;
    int32_t     gpsMode;   
    int32_t     gpioPin; 
    int32_t     initialOffsetInWSPRFreqRange;
    uint32_t    outputPowerDbm;
    int32_t     frequencyHop;

} SettingsData;

enum gpsModes {GPS_MODE_OFF = 0,GPS_MODE_AUTO = 1,GPS_MODE_ON = 2};
extern SettingsData settingsData;

// Use last sector at the top of flash for storage
extern const uint32_t FLASH_TARGET_OFFSET;
extern const uint8_t *flash_target_contents;

extern const uint32_t bandNames[NUM_BANDS];
extern const uint32_t bandFrequencies[NUM_BANDS];

void settingsReadFromFlash(bool forceReset);
void settingsWriteToFlash(void);
bool settingsCheckSettings(void);
int parse_kv(const char *input, char *key, char *value) ;
void convertToUpper(char str[]);
int bandIndexFromString(char *bandString);

void handleSettings(bool forceSettingsEntryd);
int findNextBandIndex(int currentIndex);
#endif