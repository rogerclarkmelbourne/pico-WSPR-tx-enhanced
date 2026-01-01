#ifndef PERSISTENT_STORAGE_H_
#define PERSISTENT_STORAGE_H_

#include <stdint.h>
#include <ctype.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

#define MAX_KEY 32
#define MAX_VAL 32

#define NUM_BANDS 9

extern const uint64_t  MAGIC_NUMBER ;
extern const uint32_t  CURRENT_VERSION;

typedef struct  {
    uint64_t    magicNumber;
    uint32_t    settingsVersion;
    int32_t     freqCalibrationPPM;
    uint8_t     callsign[16];
    uint8_t     locator[16];
    //int32_t     bandsBitPattern;
    uint32_t    bandIndex;
    uint8_t     slotSkip;
    uint32_t    gpsMode;   
    uint32_t    rfPin; 
    int32_t     initialOffsetInWSPRFreqRange;
    uint32_t    outputPowerDbm;
    uint32_t    frequencyHop;
    uint32_t    gpsLocation;
    uint32_t    longLocator;
    uint32_t    mode;
    uint32_t    cwSpeed;
    uint32_t    txFreq;
} SettingsData;

enum gpsModes {GPS_MODE_OFF = 0, GPS_MODE_ON};
enum operationModes {MODE_WSPR = 0, MODE_CW_BEACON, MODE_SLOW_MORSE, MODE_FT8, MODE_APRS, NUM_OPERATING_MODES};
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