///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  main.c - The project entry point.
// 
//  DESCRIPTION
//      The pico-WSPR-tx project provides WSPR beacon function using only
//  Pi Pico board. *NO* additional hardware such as freq.synth required.
//  External GPS receiver is optional and serves a purpose of holding
//  WSPR time window order and accurate frequancy drift compensation.
//
//  HOWTOSTART
//      ./build.sh; cp ./build/*.uf2 /media/Pico_Board/
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
//      Rev 0.1   18 Nov 2023
//      Rev 0.5   02 Dec 2023
//
//  PROJECT PAGE
//      https://github.com/RPiks/pico-WSPR-tx
//
//  SUBMODULE PAGE
//      https://github.com/RPiks/pico-hf-oscillator
//
//  LICENCE
//      MIT License (http://www.opensource.org/licenses/mit-license.php)
//
//  Copyright (c) 2023 by Roman Piksaykin
//  
//  Permission is hereby granted, free of charge,to any person obtaining a copy
//  of this software and associated documentation files (the Software), to deal
//  in the Software without restriction,including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY,WHETHER IN AN ACTION OF CONTRACT,TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
///////////////////////////////////////////////////////////////////////////////
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "pico/multicore.h"
#include "pico-hf-oscillator/lib/assert.h"
#include "pico-hf-oscillator/defines.h"
#include <defines.h>
#include <piodco.h>
#include <WSPRbeacon.h>
#include <logutils.h>
#include <protos.h>
#include "persistentStorage.h"


#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
//#define CONFIG_SCHEDULE_SKIP_SLOT_COUNT 5
//#define CONFIG_WSPR_DIAL_FREQUENCY 7040000UL // 18106000UL //24926000UL // 28126000UL //7040000UL
//#define CONFIG_CALLSIGN "VK3KYY"
//#define CONFIG_LOCATOR4 "QF69"

WSPRbeaconContext *pWSPR;

static void settingsErase(void);

#define NUM_BANDS 10
uint32_t bandNames[NUM_BANDS] = { 160, 80, 40, 30, 20, 17, 15, 12, 10, 6};
uint32_t bandFrequencies[NUM_BANDS] = {
         1840000,
         3560000,
         7040000,
        10136000,
        14096000,
        18106000,
        21096000,
        24926000,
        28126000,
        50293000 
};

// Read settings from flash and set to default values if no valid settings were found
void settingsReadFromFlash(void)
{
    memcpy(&settingsData,flash_target_contents,sizeof(SettingsData));
    
    if(settingsData.magicNumber != MAGIC_NUMBER || settingsData.settingsVersion != CURRENT_VERSION)
    {   
        settingsData.magicNumber        =   MAGIC_NUMBER;
        settingsData.settingsVersion    =   CURRENT_VERSION;
        settingsData.bandIndex          =   -1;// not set
        memset(settingsData.callsign, 0x00, 16);// completely erase
        memset(settingsData.locator4, 0x00, 16);// completely erase
        settingsData.slotSkip           =   -1;//not set
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
        printf("Error: Callsign not set\n");
        retVal = false;
    }
    else
    {
        printf("Callsign:%s\n",settingsData.callsign);
    }
    
    if (settingsData.locator4[0] == 0x00)
    {
        printf("Error: Maidenhead Locator not set\n");
        retVal = false;
    }
    else
    {
        printf("Location:%s\n",settingsData.locator4);   
    }
    if (settingsData.bandIndex == -1)
    {
        printf("Error: Band not set\n");
        retVal = false;
    }
    else
    {
        printf("Band:%dm\n",bandNames[settingsData.bandIndex]);   
    }

    if (settingsData.slotSkip == -1)
    {
        printf("Error: Slot Skip not set\n");
        retVal = false;
    }
    else
    {
        printf("Slot skip:%d\n",settingsData.slotSkip);   
    }
    
    return retVal;
}

int bandIndexFromString(char *bandString)
{

    if (bandString[strlen(bandString) - 1] == 'M')
    {
       bandString[strlen(bandString) - 1] = 0;// remove the M 
    }


    uint32_t bandStringNumber = atoi(bandString);

    printf("Band str is %s %d\n",bandString,bandStringNumber);


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


/**
 * Parses a command of the form KEY=VALUE.
 * Returns 1 on success, 0 on failure.
 */
#define MAX_KEY 64
#define MAX_VAL 256

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

int main()
{
    StampPrintf("\n");
    sleep_ms(5000);
    StampPrintf("R2BDY Pico-WSPR-tx start.");

    InitPicoHW();

    PioDco DCO = {0};




    StampPrintf("WSPR beacon init...");


    if (getchar())
    {
        settingsData.magicNumber = 0;
        settingsWriteToFlash();
    }

    settingsReadFromFlash();

    {
        char key[MAX_KEY];
        char value[MAX_VAL];
        char line[100];
        while (!settingsCheckSettings())
        {
            printf("Enter a Callsign,Locator, or Band in the form  SETTING=VALUE\ne.g. callsign=vk3kyy  or locator=aa11 or band=20m\n");

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

           // printf("... You entered: %s\n", line);

            convertToUpper(line);

            bool settingsAreDirty = false;
            
            if (parse_kv(line, key, value)) 
            {
                if (strcmp("CALLSIGN", key) == 0)
                {
                    strcpy(settingsData.callsign, value);

                    printf("Setting callsign to %s\n",settingsData.callsign);

                    settingsAreDirty = true;
                }
                else
                {
                    if (strcmp("LOCATOR", key) == 0)
                    {
                        strcpy(settingsData.locator4, value);

                        printf("Setting locator to %s\n",settingsData.locator4);

                        settingsAreDirty = true;
                    }
                    else
                    {
                        if (strcmp("BAND", key) == 0)
                        {
                            int newBandIndex = bandIndexFromString(value);
                            settingsData.bandIndex = newBandIndex;

                            printf("Setting band index to %d\n",settingsData.bandIndex);
                            settingsAreDirty = true;
                        }
                        else
                        {
                            if (strcmp("SLOTSKIP", key) == 0)
                            {
                                settingsData.slotSkip = atoi(value);

                                printf("Setting Slot skip to %d\n",settingsData.slotSkip);
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
            else 
            {
                printf("Invalid command format.\n\n");
                settingsCheckSettings();
                printf("\n\n");
            }

            if(settingsAreDirty)
            {
                settingsWriteToFlash();
            }
        }
    }


    WSPRbeaconContext *pWB = WSPRbeaconInit(
        settingsData.callsign,/* the Callsign. */
        settingsData.locator4,/* the default QTH locator if GPS isn't used. */
        12,             /* Tx power, dbm. */
        &DCO,           /* the PioDCO object. */
        bandFrequencies[settingsData.bandIndex],
        55UL,           /* the carrier freq. shift relative to dial freq. */
        RFOUT_PIN       /* RF output GPIO pin. */
        );
    assert_(pWB);
    pWSPR = pWB;
    
    pWB->_txSched._u8_tx_GPS_mandatory  = false;
    pWB->_txSched._u8_tx_GPS_past_time  = CONFIG_GPS_RELY_ON_PAST_SOLUTION;
    pWB->_txSched._u8_tx_slot_skip      = settingsData.slotSkip;

    multicore_launch_core1(Core1Entry);
    StampPrintf("RF oscillator started.");

    DCO._pGPStime = GPStimeInit(0, 9600, GPS_PPS_PIN);
    assert_(DCO._pGPStime);
    
    sleep_ms(2000);// allow time for any GPS NMEA message
    if (DCO._pGPStime->GpsNmeaReceived)
    {
        StampPrintf("GPS detected");
        pWB->_txSched._u8_tx_GPS_mandatory = true; 
    }


    int tick = 0;
    for(;;)
    {
        /*
        if(WSPRbeaconIsGPSsolutionActive(pWB))
        {
            const char *pgps_qth = WSPRbeaconGetLastQTHLocator(pWB);
            if(pgps_qth)
            {
                strncpy(pWB->_pu8_locator, pgps_qth, 4);
                pWB->_pu8_locator[5] = 0x00;
            }
        }
        */
       
        if(pWB->_txSched._u8_tx_GPS_mandatory)
        {
            WSPRbeaconTxScheduler(pWB, YES);
        }
        else
        {
            StampPrintf("Omitting GPS solution, start tx now.");
            PioDCOStart(pWB->_pTX->_p_oscillator);
            WSPRbeaconCreatePacket(pWB);
            sleep_ms(100);
            WSPRbeaconSendPacket(pWB);
            StampPrintf("The system will be halted when tx is completed.");
            for(;;)
            {
                if(!TxChannelPending(pWB->_pTX))
                {
                    PioDCOStop(pWB->_pTX->_p_oscillator);
                    StampPrintf("System halted.");
                }
                gpio_put(PICO_DEFAULT_LED_PIN, 1);
                sleep_ms(500);
                gpio_put(PICO_DEFAULT_LED_PIN, 0);
            }
        }
        
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(100);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);

#ifdef DEBUG
        if(0 == ++tick % 60)
            WSPRbeaconDumpContext(pWB);
#endif
        sleep_ms(900);
    }
}
