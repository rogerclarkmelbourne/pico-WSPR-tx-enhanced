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


#define CONFIG_GPS_SOLUTION_IS_MANDATORY NO
#define CONFIG_GPS_RELY_ON_PAST_SOLUTION NO
//#define CONFIG_SCHEDULE_SKIP_SLOT_COUNT 5
//#define CONFIG_WSPR_DIAL_FREQUENCY 7040000UL // 18106000UL //24926000UL // 28126000UL //7040000UL
//#define CONFIG_CALLSIGN "VK3KYY"
//#define CONFIG_LOCATOR4 "QF69"

#define BTN_PIN 21 //pin 27 on pico board
#define REPEAT_TX_EVERY_MINUTE 4 // 4 is the minimum, for longer intervals choose 6,8,10,12, ...



WSPRbeaconContext *pWSPR;


int main()
{
    StampPrintf("\n");
    sleep_ms(4000);
    StampPrintf("R2BDY Pico-WSPR-tx start.");

    InitPicoHW();
    rtc_init();
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_set_pulls(BTN_PIN,false,true);
    

    PioDco DCO = {0};

    StampPrintf("WSPR beacon init...");

    handleSettings();

    WSPRbeaconContext *pWB = WSPRbeaconInit(
        settingsData.callsign,/* the Callsign. */
        settingsData.locator4,/* the default QTH locator if GPS isn't used. */
        12,             /* Tx power, dbm. */
        &DCO,           /* the PioDCO object. */
        bandFrequencies[settingsData.bandIndex] + ((bandFrequencies[settingsData.bandIndex] / 1E6) * settingsData.freqCalibrationPPM),
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

    int initialSlotOffset;

    if (DCO._pGPStime->GpsNmeaReceived && false)
    {
        StampPrintf("GPS detected");
        pWB->_txSched._u8_tx_GPS_mandatory = true; 
        while(pWB->_pTX->_p_oscillator->_pGPStime->_time_data._u32_nmea_gprmc_count == 0)
        {
            StampPrintf("WSPR> Waiting for GPS receiver...");
            sleep_ms(1000);
        }

        int isec_of_hour = (pWB->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + ((GetUptime64() - pWB->_pTX->_p_oscillator->_pGPStime->_time_data._u64_sysclk_nmea_last) / 1000000ULL)) % HOUR;
    
        initialSlotOffset = settingsData.slotSkip - (isec_of_hour / (2 * MINUTE)) - 1;
    }
    else
    {
        // block waiting for 
        int messageCounter = 0;
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
                .min   = 0,//settingsData.slotSkip,// force immediate Tx 
                .sec   = 00
            };   

        rtc_set_datetime(&t);

        initialSlotOffset = settingsData.slotSkip;
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
       
        //if(pWB->_txSched._u8_tx_GPS_mandatory)
        {
            WSPRbeaconTxScheduler(pWB, initialSlotOffset, YES);
        }
#if false        
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
#endif        
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
