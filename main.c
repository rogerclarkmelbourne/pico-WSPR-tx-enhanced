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


#include "pico/multicore.h"
#include "pico-hf-oscillator/lib/assert.h"
#include "pico-hf-oscillator/defines.h"
#include <defines.h>
#include <piodco.h>
#include <WSPRbeacon.h>
#include <logutils.h>
#include <protos.h>

#include "pico/util/datetime.h"
#include "persistentStorage.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include "cw_beacon.h"

#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
#define BTN_PIN 21 //pin 27 on pico board




bool timer_callback(__unused repeating_timer_t *rt)
 {

    ppsTriggered = true;
    pWSPR->secondsCounter++;
    //printf("timer_callback %d\n",i);
    return true; // keep repeating
}

void rebootIntoFlashUpdateMode(void)
{ 
    reset_usb_boot(0, 0); // go to flash mode
}

WSPRbeaconContext *pWB;
void wsprLoop(void)
{
        while(true)
    {
        if(pWB->_txSched._u8_tx_GPS_mandatory && settingsData.gpsLocation)
        {
            char newMaidenHead[16];
            strcpy(newMaidenHead, WSPRbeaconGetLastQTHLocator());
            newMaidenHead[6] = 0x00;
            if(strcmp(newMaidenHead, pWB->_pu8_locator) != 0)
            {
                #ifdef DEBUG_PRINT
                    printf("Updating location from GPS %s != %s\n", newMaidenHead, pWB->_pu8_locator);
                #endif
                strcpy(pWB->_pu8_locator, newMaidenHead);
                WSPRbeaconCreatePacket(pWB->longLocatorPhase & 0x01);
            }
        }

        while(!ppsTriggered)
        {
            tight_loop_contents();
        }

        watchdog_update();
#ifdef DEBUG_PRINT
const bool debugMessages = true;
#else
const bool debugMessages = false;
#endif
        WSPRbeaconTxScheduler(debugMessages);
        ppsTriggered = false;
    }
}

int main()
{
    repeating_timer_t oneSecondTimer;// Used in conjunction with the RTC for no GPS operation
    InitPicoHW();
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_set_pulls(BTN_PIN,false,true);

    StampPrintf("\n");
    int cdcTimeoutCounter = 0;
    bool buttonHeldAtBoot = gpio_get(BTN_PIN);
    
#define DEBUG_PRINT

#ifdef WAIT_DCD
    int cdcWaitCounter = 0;
    // wait maximum of 10 seconds to serial terminal
    while (!tud_cdc_connected() & (cdcWaitCounter++ < 10)) 
    {
        sleep_ms(1000);  
    }
#endif    

    handleSettings(buttonHeldAtBoot);

    pWB = WSPRbeaconInit(
        settingsData.callsign,/* the Callsign. */
        settingsData.locator,/* the default QTH locator if GPS isn't used. */
        settingsData.outputPowerDbm,/* Tx power, dbm. */
        bandFrequencies[settingsData.bandIndex] + ((bandFrequencies[settingsData.bandIndex] / 1E6) * settingsData.freqCalibrationPPM),// bottom of WSPR freq range
        settingsData.initialOffsetInWSPRFreqRange,           /* the carrier freq. */
        settingsData.rfPin       /* RF output GPIO pin. */
        );

    
    pWB->_txSched._u8_tx_GPS_mandatory  = false;
    pWB->_txSched._u8_tx_GPS_past_time  = CONFIG_GPS_RELY_ON_PAST_SOLUTION;
    pWB->_txSched._u8_tx_slot_skip      = settingsData.slotSkip + 1;


    multicore_launch_core1(Core1Entry);

    if (settingsData.mode == MODE_WSPR)
    {
        WSPRbeaconCreatePacket(false);// first transmission must not be encoded with 6 fig locator
    }

    pWB->_pTX->_p_oscillator->_pGPStime= &gTimeContext;

    if (settingsData.gpsMode == GPS_MODE_ON)
    {
#ifdef DEBUG_PRINT    
        printf("Init GPS\n");
#endif
        GPStimeInit(0, 9600, GPS_PPS_PIN);
        sleep_ms(2000);// GPS should send data at least once per second. 
    }
    else
    {
        pWB->_pTX->_p_oscillator->_pGPStime->GpsNmeaReceived = false;
    }

    // very slow flash
    if (!add_repeating_timer_us(-2000000, ledTimer_callback, NULL, &ledFlashTimer))
    {
        while(true)
        {
            printf("Failed to add led timer\n");
            sleep_ms(1000);
        }
    }

    uint32_t initialSlotOffset;
    int messageCounter = 0;
    if (settingsData.mode == MODE_WSPR)
    {
        if (pWB->_pTX->_p_oscillator->_pGPStime->GpsNmeaReceived)
        {
            pWB->_txSched._u8_tx_GPS_mandatory = true; 

            if (!ppsTriggered)
            {
                printf("Wait for GPS PPS pulses ....");
            }
            // wait for 2 PPS pulses to guarantee stability
            for (int i=0;i<2;i++)
            {
                while(!ppsTriggered)
                {
    #ifdef DEBUG_PRINT                    
                    messageCounter++;
                    if ((messageCounter % 1000) == 0)
                    {
                        printf(".");
                    }
    #endif                
                    sleep_ms(1);
                }
                ppsTriggered = false;
            }
    #ifdef DEBUG_PRINT            
            printf("\nGPS PPS received\n");
    #endif        
            // PPS occurs at the start of the second before the RMC message is received, hence the actual time at PPS is + 1 second from the last nmea time
            int isec_of_hour = (pWB->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + 1) % HOUR;  
            
            pWB->initialSlotOffset = (settingsData.slotSkip + 1) - (isec_of_hour / (2 * MINUTE)) - 1;
        }
        else
        {
            // block waiting for button to start
            while(!gpio_get(BTN_PIN))
            {
                sleep_ms(1);
                messageCounter++;
                if ((messageCounter % 1000) == 0)
                {
                    printf("Waiting for button\n");
                }
            }

            pWB->initialSlotOffset = (settingsData.slotSkip + 1);
            ppsTriggered = true;

            // use frequency calibration ppm value, because it will also affect the timers.
            // However to increase the timer frequency, the callback time needs to be reduced instead of increased in the case of the frequency
            // Hence the the value is deducted from the 1E6 us value
            if (!add_repeating_timer_us(-1 * (1000000 - settingsData.freqCalibrationPPM), timer_callback, NULL, &oneSecondTimer))
            {
                while(true)
                {
                    printf("Failed to add one second timer\n");
                    sleep_ms(1000);
                }
            }
        }

        srand(get_absolute_time());// For frequency hopping

        watchdog_enable(10000, 1);
    }
    int tick = 0;

    char lastMaidenHead[16];
    lastMaidenHead[0] = 0;
    if(pWB->_txSched._u8_tx_GPS_mandatory && settingsData.gpsLocation)
    {
        strcpy(lastMaidenHead, WSPRbeaconGetLastQTHLocator());
        strcpy(pWB->_pu8_locator, lastMaidenHead);
        pWB->_pu8_locator[6] = 0x00;
        WSPRbeaconCreatePacket(pWB->longLocatorPhase & 0x01);
    }

    switch (settingsData.mode)
    {
        case MODE_WSPR:
            wsprLoop();
            break;
        case MODE_CW_BEACON:
        case MODE_SLOW_MORSE:
            handleCW();
            break;
    }
}
