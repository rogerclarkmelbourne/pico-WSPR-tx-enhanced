#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <piodco.h>

#include "hardware/watchdog.h"
#include "persistentStorage.h"

const uint64_t  MAGIC_NUMBER    = 0x5069636F57535052;// 'PicoWSPR  
const uint32_t  CURRENT_VERSION = 0x01;

SettingsData settingsData;

// Use last sector at the top of flash for storage
const uint32_t FLASH_TARGET_OFFSET = (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

const uint32_t bandNames[NUM_BANDS] = { 160, 80, 40, 30, 20, 17, 15, 12, 10};
const uint32_t bandFrequencies[NUM_BANDS] = {
         1838000,
         3570000,
         7040000,
        10140100,
        14097000,
        18106000,
        21096000,
        24926000,
        28126000
};



/**
 * Parses a command of the form KEY=VALUE.
 * Returns 1 on success, 0 on failure.
 */


int parse_kv(const char *input, char *key, char *value) 
{
    const char *eq = strchr(input, '=');

    if (!eq)
    {
        return 0;           // '=' not found → invalid format
    }

    size_t key_len = eq - input;
    if (key_len == 0 || key_len >= MAX_KEY)
    {
        return 0;               // key empty or too long
    }
    // Copy key
    strncpy(key, input, key_len);
    key[key_len] = '\0';

    // Copy value (may be empty, depending on your rules)
    const char *val_start = eq + 1;
    if (strlen(val_start) >= MAX_VAL)
    {
        return 0;
    }

    strcpy(value, val_start);

    return 1;
}


void convertToUpper(char str[])
 {
    int i = 0;

    while (str[i] != '\0')
    { 
        str[i] = toupper(str[i]); 
        i++;
    }
}





// Read settings from flash and set to default values if no valid settings were found
void settingsReadFromFlash(void)
{
    memcpy(&settingsData,flash_target_contents,sizeof(SettingsData));
    
    if(settingsData.magicNumber != MAGIC_NUMBER || settingsData.settingsVersion != CURRENT_VERSION)
    {   
        settingsData.magicNumber        =   MAGIC_NUMBER;
        settingsData.settingsVersion    =   CURRENT_VERSION;
        settingsData.bandsBitPattern        =   0B100;// 40m
        settingsData.freqCalibrationPPM =   0;// Default this no calibration offset
        memset(settingsData.callsign, 0x00, 16);// completely erase
        memset(settingsData.locator4, 0x00, 16);// completely erase
        settingsData.slotSkip           =   1;// Every other slot
        settingsData.gpioPin            = RFOUT_PIN;
        settingsData.frequencyHop       = false;
        settingsData.initialOffsetInWSPRFreqRange = 0;
        settingsWriteToFlash();
    }
}

void settingsWriteToFlash(void)
{
    printf("Storing settings in Flash memory\n");
    uint32_t interrupts = save_and_disable_interrupts();
    
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE ); 

    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&settingsData, FLASH_SECTOR_SIZE);

    restore_interrupts(interrupts);
}

bool settingsCheckSettings(void)
{
    bool retVal = true;

    if (settingsData.callsign[0] == 0x00)
    {
        printf("Error: CALLSIGN not set\n");
        retVal = false;
    }
    else
    {
        printf("CALLSIGN:%s\n",settingsData.callsign);
    }
    
    if (settingsData.locator4[0] == 0x00)
    {
        printf("Error: LOCATOR not set\n");
        retVal = false;
    }
    else
    {
        printf("LOCATOR:%s\n",settingsData.locator4);   
    }
    if (settingsData.bandsBitPattern == 0)
    {
        printf("Error: No bands set\n");
        retVal = false;
    }
    else
    {
        uint32_t bitPattern = settingsData.bandsBitPattern;
        printf("BAND(s):");

        for(int i=0;i<NUM_BANDS;i++)
        {
            if(bitPattern & 0x00000001)
            {
                printf("%dm ",bandNames[i]);   
            }
            bitPattern = bitPattern >> 1;
        }
        printf("\n");
    }

    if (settingsData.slotSkip == -1)
    {
        printf("Error: SLOTSKIP not set\n");
        retVal = false;
    }
    else
    {
        printf("SLOTSKIP:%d\n",settingsData.slotSkip);   
    }


    printf("CALPPM:%d\n", settingsData.freqCalibrationPPM);

    printf("GPIOPIN:%d\n", settingsData.gpioPin);


    printf("FREQHOP:%s\n", settingsData.frequencyHop?"YES":"NO");
 
    printf("OFFSET:%d\n", settingsData.initialOffsetInWSPRFreqRange);


    char *msg;
    switch(settingsData.gpsMode)
    {
        case GPS_MODE_OFF:
            msg = "Off";
            break;
        case GPS_MODE_AUTO:
             msg = "Auto";
            break;
        case GPS_MODE_ON:
            msg = "On";
            break;
    }
    printf("GPS: %d %s\n", settingsData.gpsMode, msg);

    return retVal;
}

int bandIndexFromString(char *bandString)
{

    if (bandString[strlen(bandString) - 1] == 'M')
    {
       bandString[strlen(bandString) - 1] = 0;// remove the M 
    }


    uint32_t bandStringNumber = atoi(bandString);


    int  bandIndexFound = -1;
    for(int i=0; i<NUM_BANDS; i++)
    {
        if (bandStringNumber == bandNames[i])
        {
            return i;
        }
    }

    return -1; // band not found
}

int findNextBandIndex(int currentIndex)
{

    uint32_t bitPattern = settingsData.bandsBitPattern >> currentIndex;

    while(currentIndex < NUM_BANDS)
    {
        if(bitPattern & 0x01)
        {
            return currentIndex;
        }
        currentIndex++;
        bitPattern = bitPattern >> 1;
    }
}


void handleSettings(bool forceSettingsEntry)
{
    settingsReadFromFlash();
    
    if (!settingsCheckSettings() || forceSettingsEntry)
    {
        char key[MAX_KEY];
        char value[MAX_VAL];
        char line[100];

        while (true)
        {
            settingsCheckSettings();
            printf("Enter setting in the form SETTING=VALUE\ne.g. CALLSIGN=VK3KYY or LOCATOR=AA00\n\n");

            int idx = 0;
            for (;;)
            {
                int ch = getchar();   // non-blocking read. Returns 0 if no char in buffer

                if (ch)
                {
                    if (ch == '\r' || ch == '\r' ) 
                    {
                        line[idx] = '\0';
                        break;            // finished reading a full line
                    }
                    else
                    {
                        if (idx < (int)sizeof(line) - 1) 
                        {
                            putchar(ch);// echo back to terminal
                            line[idx++] = ch;
                        }
                    }
                }

                sleep_ms(1);
            }

            convertToUpper(line);

            bool settingsAreDirty = false;

            if (strcmp(line,"REBOOT") == 0)
            {
                printf("Exiting settings. Rebooting...");
                watchdog_enable(1, 1);
                while(true);
            }

            if (strcmp(line,"EXIT") == 0)
            {
                printf("Exiting settings.");
                return;
            }
            
            if (parse_kv(line, key, value)) 
            {
                if (strcmp("CALLSIGN", key) == 0)
                {
                    strncpy(settingsData.callsign, value, 16);

                    printf("\nSetting callsign to %s\n",settingsData.callsign);

                    settingsAreDirty = true;
                }
                else
                {
                    if (strcmp("LOCATOR", key) == 0)
                    {
                        strncpy(settingsData.locator4, value, 16);

                        printf("\nSetting locator to %s\n",settingsData.locator4);

                        settingsAreDirty = true;
                    }
                    else
                    {
                        if (strcmp("BAND", key) == 0)
                        {
                            int bandIndex = bandIndexFromString(value);

                            if (bandIndex!= -1)
                            {

                                uint32_t pattern = 1 << bandIndex;

                                settingsData.bandsBitPattern ^= pattern;
 
                                settingsAreDirty = true;
                            }
                            else
                            {
                                printf("\nError: Unknown band\n");
                            }
                        }
                        else
                        {
                            if (strcmp("SLOTSKIP", key) == 0)
                            {
                                int slotSkip = atoi(value);
                                if (slotSkip > 0 && slotSkip <= 100)
                                {
                                    settingsData.slotSkip = atoi(value);

                                    printf("\nSetting Slot skip to %d\n",settingsData.slotSkip);
                                    settingsAreDirty = true;
                                }
                                else
                                {
                                    printf("\nERROR: Slot skip must be between 1 and 100 inclusive\n");
                                }
                            }
                            else
                            {
                                if (strcmp("CALPPM", key) == 0)
                                {
                                    settingsData.freqCalibrationPPM = atoi(value);

                                    printf("\nSetting frequency calibration to %d ppm\n",settingsData.freqCalibrationPPM);
                                    settingsAreDirty = true;
                                }
                                else
                                {
                                    if (strcmp("GPIOPIN", key) == 0)
                                    {
                                        settingsData.gpioPin = atoi(value);

                                        printf("\nSetting GPIO pin to %d ppm\n",settingsData.gpioPin);
                                        settingsAreDirty = true;
                                    }
                                    else
                                    {
                                        if (strcmp("OFFSET", key) == 0)
                                        {
                                            settingsData.initialOffsetInWSPRFreqRange = atoi(value);

                                            printf("\nSetting initial freq offset to %d ppm\n",settingsData.initialOffsetInWSPRFreqRange);
                                            settingsAreDirty = true;
                                        }
                                        else
                                        {
                                            if (strcmp("FREQHOP", key) == 0)
                                            {
                                                settingsData.frequencyHop = (strcmp(value,"ON") == 0);

                                                printf("\nSetting frequency hop to %s\n",settingsData.frequencyHop?"ON":"OFF");
                                                settingsAreDirty = true;
                                            }
                                            else
                                            {
                                                if (strcmp("GPS", key) == 0)
                                                {
                                                    if (strcmp(value,"OFF") == 0)
                                                    {
                                                        settingsData.gpsMode = GPS_MODE_OFF;
                                                        printf("\nSetting GPS mode to OFF\n");
                                                    }
                                                    else
                                                    {
                                                        if (strcmp(value,"AUTO") == 0)
                                                        {
                                                            settingsData.gpsMode = GPS_MODE_AUTO;
                                                            printf("\nSetting GPS mode to AUTO\n");
                                                        }
                                                    }

                                                    settingsAreDirty = true;
                                                }
                                                else
                                                {
                                                    printf("Uknown setting\n");
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } 
            else 
            {
                printf("\nInvalid command format.\n\n");
                settingsCheckSettings();
                printf("\n\n");
            }

            if(settingsAreDirty)
            {
                settingsWriteToFlash();
            }
        }
    }    
}