#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <piodco.h>


#include "persistentStorage.h"

const uint64_t  MAGIC_NUMBER    = 0x5069636F57535052;// 'PicoWSPR  
const uint32_t  CURRENT_VERSION = 0x00;

SettingsData settingsData;

// Use last sector at the top of flash for storage
const uint32_t FLASH_TARGET_OFFSET = (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

const uint32_t bandNames[NUM_BANDS] = { 160, 80, 40, 30, 20, 17, 15, 12, 10, 6};
const uint32_t bandFrequencies[NUM_BANDS] = {
         1838000,
         3570000,
         7040000,
        10140100,
        14097000,
        18106000,
        21096000,
        24926000,
        28126000,
        50294400 
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
        settingsData.bandIndex          =   2;// 40m
        settingsData.freqCalibrationPPM =   0;// Default this no calibration offset
        memset(settingsData.callsign, 0x00, 16);// completely erase
        memset(settingsData.locator4, 0x00, 16);// completely erase
        settingsData.slotSkip           =   2;
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
    if (settingsData.bandIndex == -1)
    {
        printf("Error: BAND not set\n");
        retVal = false;
    }
    else
    {
        printf("BAND:%dm\n",bandNames[settingsData.bandIndex]);   
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


void handleSettings(bool buttonIsPressed)
{

    settingsReadFromFlash();
    
    if (!settingsCheckSettings() || getchar() || buttonIsPressed)
    {
        char key[MAX_KEY];
        char value[MAX_VAL];
        char line[100];

        while (true)
        {
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
            
            if (parse_kv(line, key, value)) 
            {
                if (strcmp("CALLSIGN", key) == 0)
                {
                    strcpy(settingsData.callsign, value);

                    printf("\nSetting callsign to %s\n",settingsData.callsign);

                    settingsAreDirty = true;
                }
                else
                {
                    if (strcmp("LOCATOR", key) == 0)
                    {
                        strcpy(settingsData.locator4, value);

                        printf("\nSetting locator to %s\n",settingsData.locator4);

                        settingsAreDirty = true;
                    }
                    else
                    {
                        if (strcmp("BAND", key) == 0)
                        {
                            int newBandIndex = bandIndexFromString(value);
                            if (newBandIndex!= -1)
                            {
                                settingsData.bandIndex = newBandIndex;

                                printf("\nSetting band to %dm\n", bandNames[settingsData.bandIndex]);
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
                                settingsData.slotSkip = atoi(value);

                                printf("\nSetting Slot skip to %d\n",settingsData.slotSkip);
                                settingsAreDirty = true;
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
                                    printf("Uknown setting\n");
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