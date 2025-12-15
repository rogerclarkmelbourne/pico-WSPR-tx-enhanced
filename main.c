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

#include "hardware/rtc.h"
#include "pico/util/datetime.h"
#include "persistentStorage.h"
#include "hardware/watchdog.h"

#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
#define BTN_PIN 21 //pin 27 on pico board

bool timer_callback(__unused repeating_timer_t *rt)
 {

    ppsTriggered = true;
    //printf("timer_callback %d\n",i);
    return true; // keep repeating
}

int main()
{
    repeating_timer_t oneSecondTimer;// Used in conjunction with the RTC for no GPS operation
    InitPicoHW();
    rtc_init();
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_set_pulls(BTN_PIN,false,true);

    StampPrintf("\n");
    handleSettings(false);

    for(int i=0;i<10;i++)
    {
        printf("Wait for keypress to enter settings\n");
        if (gpio_get(BTN_PIN) || getchar()) 
        {
            handleSettings(true);
        }
        else
        {
            sleep_ms(1000);
        }
    }


    WSPRbeaconContext *pWB = WSPRbeaconInit(
        settingsData.callsign,/* the Callsign. */
        settingsData.locator4,/* the default QTH locator if GPS isn't used. */
        settingsData.outputPowerDbm,/* Tx power, dbm. */
        bandFrequencies[findNextBandIndex(0)] + ((bandFrequencies[findNextBandIndex(0)] / 1E6) * settingsData.freqCalibrationPPM),// bottom of WSPR freq range
        settingsData.initialOffsetInWSPRFreqRange,           /* the carrier freq. */
        settingsData.gpioPin       /* RF output GPIO pin. */
        );

    
    pWB->_txSched._u8_tx_GPS_mandatory  = false;
    pWB->_txSched._u8_tx_GPS_past_time  = CONFIG_GPS_RELY_ON_PAST_SOLUTION;
    pWB->_txSched._u8_tx_slot_skip      = settingsData.slotSkip + 1;

    multicore_launch_core1(Core1Entry);
    StampPrintf("RF oscillator started.");

    // Location, callsign and power data does not change, so we only need to create it once.
    WSPRbeaconCreatePacket();


    pWB->_pTX->_p_oscillator->_pGPStime= &gTimeContext;

    if (settingsData.gpsMode == GPS_MODE_AUTO)
    {
        printf("Init GPS\n");
        GPStimeInit(0, 9600, GPS_PPS_PIN);
        sleep_ms(5000);// GPS should send data at least once per second. 
    }
    else
    {
        pWB->_pTX->_p_oscillator->_pGPStime->GpsNmeaReceived = false;
        printf("IGNORE GPS\n");
    }

    uint32_t initialSlotOffset;
    int messageCounter = 0;

    if (pWB->_pTX->_p_oscillator->_pGPStime->GpsNmeaReceived)
    {
        pWB->_txSched._u8_tx_GPS_mandatory = true; 

        if (!ppsTriggered)
        {
            printf("PPS Wait....");
        }
        // wait for 10 PPS pulses to guarantee stability
        for (int i=0;i<2;i++)
        {
            while(!ppsTriggered)
            {
                messageCounter++;
                if ((messageCounter % 1000) == 0)
                {
                    printf(".");
                }
                sleep_ms(1);
            }
            ppsTriggered = false;
        }
        printf("\nPPS received\n");
        // PPS occurs at the start of the second before the RMC message is received, hence the actual time at PPS is + 1 second from the last nmea time
        int isec_of_hour = (pWB->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + 1) % HOUR;  
         
        initialSlotOffset = (settingsData.slotSkip + 1) - (isec_of_hour / (2 * MINUTE)) - 1;
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

        datetime_t t = {
                .year  = 2024,
                .month = 01,
                .day   = 01,
                .dotw  = 0, 
                .hour  = 0,
                .min   = 0,
                .sec   = 0
            };   
        rtc_set_datetime(&t);

        initialSlotOffset = (settingsData.slotSkip + 1);
        ppsTriggered = true;

        const int hz = 1;// once per second

        if (!add_repeating_timer_us(-1000000 / hz, timer_callback, NULL, &oneSecondTimer))
        {
            while(true)
            {
                printf("Failed to add timer\n");
            }
        }
    }

    srand(get_absolute_time());// For frequency hopping

    watchdog_enable(2000, 1);
    int tick = 0;

    // slow flash
    if (!add_repeating_timer_us(-2000000, ledTimer_callback, NULL, &ledFlashTimer))
    {
        while(true)
        {
            printf("Failed to add led timer\n");
        }
    }

    while(true)
    {
        /*
        if(WSPRbeaconIsGPSsolutionActive())
        {
            const char *pgps_qth = WSPRbeaconGetLastQTHLocator();
            if(pgps_qth)
            {
                strncpy(pWB->_pu8_locator, pgps_qth, 4);
                pWB->_pu8_locator[5] = 0x00;
            }
        }
        */

        while(!ppsTriggered)
        {
            tight_loop_contents();
        }
        WSPRbeaconTxScheduler(initialSlotOffset, false);
        ppsTriggered = false;
        watchdog_update();


#if (defined(DEBUG) && false)
        if(0 == ++tick % 60)
            WSPRbeaconDumpContext(pWB);
#endif
    }
}
