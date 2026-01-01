#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <piodco.h>
// for USB CDX
#include "tusb.h"

#include "hardware/watchdog.h"
#include "persistentStorage.h"

const uint64_t  MAGIC_NUMBER    = 0x5069636F57535052;// 'PicoWSPR  
const uint32_t  CURRENT_VERSION = 14;

SettingsData settingsData;

// Use last sector at the top of flash for storage
const uint32_t FLASH_TARGET_OFFSET = (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

const uint32_t bandNames[NUM_BANDS] = { 160, 80, 40, 30, 20, 17, 15, 12, 10 };
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

const char *OPERATING_MODES[NUM_OPERATING_MODES] = {"WSPR","CW","SLOWMORSE","FT8","APRS"};

/**
 * Parses a command of the form KEY=VALUE.
 * Returns 1 on success, 0 on failure.
 */


int parse_kv(const char *input, char *key, char *value) 
{
    const char *eq = strchr(input, ' ');

    if (!eq)
    {
        return 0;           // ' ' not found → invalid format
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
void settingsReadFromFlash(bool forceReset)
{
    memcpy(&settingsData,flash_target_contents,sizeof(SettingsData));
    
    if(settingsData.magicNumber != MAGIC_NUMBER || settingsData.settingsVersion != CURRENT_VERSION || forceReset)
    {   
        settingsData.magicNumber        =   MAGIC_NUMBER;
        settingsData.settingsVersion    =   CURRENT_VERSION;
        //settingsData.bandsBitPattern    =   0B100;// 40m
        settingsData.bandIndex = 2;// 40m
        settingsData.freqCalibrationPPM =   0;// Default this no calibration offset
        memset(settingsData.callsign, 0x00, 16);// completely erase
        memset(settingsData.locator, 0x00, 16);// completely erase
        settingsData.slotSkip           =   4;// Every 5th slot
        settingsData.rfPin            = RFOUT_PIN;
        settingsData.frequencyHop       = false;
        settingsData.gpsMode            = GPS_MODE_ON;
        settingsData.initialOffsetInWSPRFreqRange = 0;
        settingsData.outputPowerDbm = 13;
        settingsData.gpsLocation = 0;
        settingsData.longLocator = 0;
        settingsData.mode = MODE_CW_BEACON;
        settingsData.cwSpeed = 5;
        settingsData.txFreq = 7010000;//7.050Mhz        

        settingsWriteToFlash();
    }
}

void settingsWriteToFlash(void)
{
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
    
    if (settingsData.locator[0] == 0x00)
    {
        printf("Error: LOCATOR not set\n");
        retVal = false;
    }
    else
    {
        printf("LOCATOR:%s\n",settingsData.locator);   
    }

#ifdef BANDS_BIT_PATTERN    
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
#endif

    switch(settingsData.mode)
    {
        case MODE_WSPR:
            printf("Band: %dm\n",bandNames[settingsData.bandIndex]); 

            if (settingsData.slotSkip == -1)
            {
                printf("Error: SLOTSKIP not set\n");
                retVal = false;
            }
            else
            {
                printf("SLOTSKIP:%d\n",settingsData.slotSkip);   
            }

            printf("OFFSET:%d\n", settingsData.initialOffsetInWSPRFreqRange);
            printf("FREQHOP:%s\n", settingsData.frequencyHop?"On":"Off");
            printf("POWER:%d\n", settingsData.outputPowerDbm);
            break;
        case MODE_CW_BEACON:
        case MODE_SLOW_MORSE:
            printf("TXFREQ:%d\n", settingsData.txFreq);
            printf("CWSPEED:%d WPM\n", settingsData.cwSpeed);
            break;         
        case MODE_FT8:
            break;    
        case MODE_APRS:
            printf("TXFREQ:%d\n", settingsData.txFreq);        
            break;                  
    }

    printf("CALPPM:%d\n", settingsData.freqCalibrationPPM);

    printf("RFPIN:%d\n", settingsData.rfPin);

  
    printf("MODE:%s\n",OPERATING_MODES[settingsData.mode]);

    printf("GPSLOCATION:%s\n", settingsData.gpsLocation?"On":"Off");

    printf("LONGLOCATOR:%s\n", settingsData.longLocator?"On":"Off");

    char *msg;
    switch(settingsData.gpsMode)
    {
        case GPS_MODE_OFF:
            msg = "Off";
            break;
        case GPS_MODE_ON:
            msg = "On";
            break;
    }
    printf("GPS: %s\n", msg);



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
#ifdef BANDS_BIT_PATTERN   
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
#endif

void handleSettings(bool forceSettingsEntry)
{
    settingsReadFromFlash(false);
    
    if (!settingsCheckSettings() || forceSettingsEntry)
    {
        char key[MAX_KEY];
        char value[MAX_VAL];
        char line[100];

        // If settings need to be update, we need to wait for the USB Serial terminal to be opened
        while (!tud_cdc_connected()) 
        {
            sleep_ms(100);  
        }
  
        printf("Firmware was built at %s on %s\n", __TIME__, __DATE__);        

        while (true)
        {
            settingsCheckSettings();
            
            printf("Enter setting.  e.g. CALLSIGN VK3KYY\n\n");

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
                printf("Exiting settings. Rebooting...\n");
                watchdog_enable(1, 1);
                while(true);
            }

            if (strcmp(line,"EXIT") == 0)
            {
                printf("Exiting settings.\n");
                if (settingsCheckSettings())
                {
                    return;
                }
            }

            if (strcmp(line,"RESET") == 0)
            {
                printf("Clearing settings.\n");
                settingsReadFromFlash(true);
                settingsCheckSettings();
            }
            
            
            if (parse_kv(line, key, value)) 
            {
                for(;;)
                {

                    if (strcmp("MODE", key) == 0)
                    {
                        int newMode = -1;

                        for(int i=0;i<NUM_OPERATING_MODES;i++)
                        {
                            if (strcmp(OPERATING_MODES[i],value) == 0)
                            {
                                newMode = i;
                                break;
                            }
                        }

                        if (newMode != -1)
                        {
                            settingsData.mode = newMode;

                            printf("\nSetting mode to %s\n",OPERATING_MODES[newMode]);

                            settingsAreDirty = true;                        
                        }
                        else
                        {
                            printf("\nInvalid mode\n");
                        }
                        break;

                    }

                    if (strcmp("CALLSIGN", key) == 0)
                    {
                        strncpy(settingsData.callsign, value, 16);

                        printf("\nSetting callsign to %s\n",settingsData.callsign);

                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("LOCATOR", key) == 0)
                    {
                        strncpy(settingsData.locator, value, 16);

                        printf("\nSetting locator to %s\n",settingsData.locator);

                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("BAND", key) == 0)
                    {
                        int bandIndex = bandIndexFromString(value);

                        if (bandIndex!= -1 && bandIndex < NUM_BANDS)
                        {
                            settingsData.bandIndex = bandIndex;
#ifdef BANDS_BIT_PATTERN   
                            uint32_t pattern = 1 << bandIndex;

                            settingsData.bandsBitPattern ^= pattern;
#endif
                            printf("\nSetting Band to %dm\n",bandNames[settingsData.bandIndex]); 
                            settingsAreDirty = true;
                        }
                        else
                        {
                            printf("\nError: Unknown band\n");
                        }
                        break;
                    }

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
                        break;
                    }

                    if (strcmp("CALPPM", key) == 0)
                    {
                        settingsData.freqCalibrationPPM = atoi(value);

                        printf("\nSetting frequency calibration to %d ppm\n",settingsData.freqCalibrationPPM);
                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("RFPIN", key) == 0)
                    {
                        settingsData.rfPin = atoi(value);

                        printf("\nSetting RF pin to %d ppm\n",settingsData.rfPin);
                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("OFFSET", key) == 0)
                    {
                        settingsData.initialOffsetInWSPRFreqRange = atoi(value);

                        printf("\nSetting initial freq offset to %d ppm\n",settingsData.initialOffsetInWSPRFreqRange);
                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("FREQHOP", key) == 0)
                    {
                        settingsData.frequencyHop = (strcmp(value,"ON") == 0);

                        printf("\nSetting frequency hop to %s\n",settingsData.frequencyHop?"On":"Off");
                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("GPS", key) == 0)
                    {
                        if (strcmp(value,"OFF") == 0)
                        {
                            settingsData.gpsMode = GPS_MODE_OFF;
                            printf("\nSetting GPS mode to Off\n");
                        }
                        else
                        {
                            if (strcmp(value,"ON") == 0)
                            {
                                settingsData.gpsMode = GPS_MODE_ON;
                                printf("\nSetting GPS mode to On\n");
                            }
                        }

                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("POWER", key) == 0)
                    {
                        settingsData.outputPowerDbm = atoi(value);

                        printf("\nSetting Power to %d dBm\n",settingsData.outputPowerDbm);

                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("GPSLOCATION", key) == 0)
                    {
                        if (strcmp(value,"ON") == 0)
                        {
                            settingsData.gpsLocation = 1;
                            printf("\nSetting GPS location to On\n");
                        }
                        else
                        {
                            settingsData.gpsLocation = 0;
                            printf("\nSetting GPS location to Off\n");
                        }

                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("LONGLOCATOR", key) == 0)
                    {
                        if (strcmp(value,"ON") == 0)
                        {
                            settingsData.longLocator = 1;
                            printf("\nSetting Long Locator to On\n");
                        }
                        else
                        {
                            settingsData.longLocator = 0;
                            printf("\nSetting Long Locator to Off\n");
                        }

                        settingsAreDirty = true;
                        break;
                    }

                    if (strcmp("TXFREQ", key) == 0)
                    {
                        settingsData.txFreq = atoi(value);

                        printf("\nSetting Tx Freq to %d\n",settingsData.txFreq);

                        settingsAreDirty = true;
                        break;
                    }      
                    
                    if (strcmp("CWSPEED", key) == 0)
                    {
                        settingsData.cwSpeed = atoi(value);

                        printf("\nSetting CW Speed to %d\n",settingsData.cwSpeed);

                        settingsAreDirty = true;
                        break;
                    }     
                    break;                
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