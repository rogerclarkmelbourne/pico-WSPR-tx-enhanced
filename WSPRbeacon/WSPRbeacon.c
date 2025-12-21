///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  WSPRbeacon.c - WSPR beacon - related functions.
// 
//  DESCRIPTION
//      The pico-WSPR-tx project provides WSPR beacon function using only
//  Pi Pico board. *NO* additional hardware such as freq.synth required.
//
//  HOWTOSTART
//  .
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
// 
//      Rev 0.1   18 Nov 2023
//  Initial release.
//
//  PROJECT PAGE
//      https://github.com/RPiks/pico-WSPR-tx
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
#include "WSPRbeacon.h"
#include <WSPRutility.h>
#include <maidenhead.h>
#include "persistentStorage.h"
#include <pico/time.h>

WSPRbeaconContext becaconData = {0};
WSPRbeaconContext *pWSPR = &becaconData;

repeating_timer_t ledFlashTimer;

static int ledTick = 0;
static int lastOffsetFreq = 0;

bool ledTimer_callback(__unused repeating_timer_t *rt)
 {
    gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));// Toggle LED state
    //printf("timer_callback %d\n",i);
    return true; // keep repeating
}

/// @brief Initializes a new WSPR beacon context.
/// @param pcallsign HAM radio callsign, 12 chr max.
/// @param pgridsquare Maidenhead locator, 7 chr max.
/// @param txpow_dbm TX power, db`mW.
/// @param pdco Ptr to working DCO.
/// @param dial_freq_hz The begin of working WSPR passband.
/// @param shift_freq_hz The shift of tx freq. relative to dial_freq_hz.
/// @param gpio Pico's GPIO pin of RF output.
/// @return Ptr to the new context.
WSPRbeaconContext * WSPRbeaconInit(const char *pcallsign, const char *pgridsquare, int txpow_dbm,
                                  uint32_t dial_freq_hz, int32_t shift_freq_hz,
                                  int gpio)
{
    strncpy(becaconData._pu8_callsign, pcallsign, sizeof(becaconData._pu8_callsign));
    strncpy(becaconData._pu8_locator, pgridsquare, sizeof(becaconData._pu8_locator));
    becaconData._u8_txpower = txpow_dbm;

    becaconData._pTX = TxChannelInit(682667, 0);
    if (!becaconData._pTX)
    {
        printf("Failed to initialise 'Channel' data.\nUnable to continue\n");
        while(true) 
        {
            sleep_ms(1000);
        }
    }

    TxChannelSetFrequency(dial_freq_hz, shift_freq_hz);

    becaconData._pTX->_i_tx_gpio = gpio;

    return &becaconData;
}

/// @brief Sets dial (baseband minima) freq.
/// @param pctx Context.
/// @param freq_hz the freq., Hz.
void WSPRbeaconSetDialFreq(uint32_t freq_hz)
{

    becaconData._pTX->_u32_Txfreqhz = freq_hz;
}

/// @brief Constructs a new WSPR packet using the data available.
/// @param pctx Context
/// @return 0 if OK.
int WSPRbeaconCreatePacket(void)
{
    wspr_encode(becaconData._pu8_callsign, becaconData._pu8_locator, becaconData._u8_txpower, becaconData._pu8_outbuf);

    return 0;
}

/// @brief Sends a prepared WSPR packet using TxChannel.
/// @param pctx Context.
/// @return 0, if OK.
int WSPRbeaconSendPacket(void)
{
    assert_(becaconData._pTX);
    assert_(becaconData._pTX->_u32_Txfreqhz > 500 * kHz);

    TxChannelClear(becaconData._pTX);

    memcpy(becaconData._pTX->_pbyte_buffer, becaconData._pu8_outbuf, WSPR_SYMBOL_COUNT);
    becaconData._pTX->_ix_input = WSPR_SYMBOL_COUNT;



    TxChannelStart();

    return 0;
}

/// @brief Arranges WSPR sending in accordance with pre-defined schedule.
/// @brief It works only if GPS receiver available (for now).
/// @param pctx Ptr to Context.
/// @param verbose Whether stdio output is needed.
/// @return 0 if OK, -1 if NO GPS received available


uint32_t lastNmeaRmcCount = 0;
uint32_t lastSkipSlotModuloDisplayed = 0;
int itx_trigger = 0;
uint32_t lastIntDisplayed = 0;

int WSPRbeaconTxScheduler(int verbose)
{
    bool debugPrint = verbose;

    uint32_t isec_of_hour;
    uint32_t islot_number;
    uint32_t islot_modulo;

    if( becaconData._txSched._u8_tx_GPS_mandatory)
    {
        // PPS occurs at the start of the second before the RMC message is received, hence the actual time at PPS is + 1 second from the last nmea time
        isec_of_hour = (becaconData._pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + 1) % HOUR;
    }
    else
    {
        isec_of_hour = becaconData.secondsCounter % HOUR;       
    }

    islot_number = (isec_of_hour  / (2 * MINUTE)) + becaconData.initialSlotOffset;
    islot_modulo = islot_number % becaconData._txSched._u8_tx_slot_skip;

    uint32_t secsIntoCurrentSlot = (isec_of_hour % (2 * MINUTE));
    
    if (debugPrint)
    {
        printf("Slot %d. Seconds %d. %s\n" , islot_modulo, secsIntoCurrentSlot, itx_trigger?"Tx":"Rx");
    }

    if(0 == islot_modulo)
    {
        if(!itx_trigger)
        {
            if (secsIntoCurrentSlot == 1)
            {
                itx_trigger = 1;

                WSPRbeaconSendPacket();
               
                printf("WSPR> Start TX.\n");

                ledFlashTimer.delay_us = 500000;
            }
        }
        else
        {
            // Check if Tx has finished and Osc has been turned off
            if (!becaconData._pTX->_p_oscillator->_is_enabled)
            {
                ledFlashTimer.delay_us = 2000000;
 
                printf("WSPR> End Tx. @ %d secs\n",secsIntoCurrentSlot);

                if (settingsData.frequencyHop)
                {
                    // Set the freq of the next transmission now.
                    const int FREQ_STEP_SIZE = 5;// Hz
                    const int rangeInHzSteps = (WSPR_FREQ_RANGE_HZ - 10) / FREQ_STEP_SIZE;// The -10 is so that the freq hot doesn't use the 5Hz at the top and bottom of the range as the modulation is 6Hz wide
                    int r,offset;
                    do
                    {
                        r = (rand() % rangeInHzSteps) - (rangeInHzSteps/2);
                        offset = r * FREQ_STEP_SIZE;
                    } while (lastOffsetFreq == offset);
                    lastOffsetFreq = offset;

                    printf("Offset frequency %d Hz\n",offset);
                    TxChannelSetOffsetFrequency(offset);
                }
                itx_trigger = 0;
            }
        }
    }

    return 0;
}

/// @brief Dumps the beacon context to stdio.
/// @param pctx Ptr to Context.
void WSPRbeaconDumpContext(void)
{
    assert_(becaconData._pTX);

    const uint64_t u64tmnow = GetUptime64();
    const uint64_t u64_GPS_last_age_sec 
        = (u64tmnow - becaconData._pTX->_p_oscillator->_pGPStime->_time_data._u64_sysclk_nmea_last) / 1000000ULL;

    StampPrintf("__________________");
    StampPrintf("=TxChannelContext=");
    StampPrintf("ftc:%llu", becaconData._pTX->_tm_future_call);
    StampPrintf("ixi:%u", becaconData._pTX->_ix_input);
    StampPrintf("dfq:%lu", becaconData._pTX->_u32_Txfreqhz);
    StampPrintf("gpo:%u", becaconData._pTX->_i_tx_gpio);

    GPStimeContext *pGPS = becaconData._pTX->_p_oscillator->_pGPStime;
    const uint32_t u32_unixtime_now 
            = becaconData._pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + u64_GPS_last_age_sec;
    assert_(pGPS);
    StampPrintf("=GPStimeContext=");
    StampPrintf("err:%ld", pGPS->_i32_error_count);
    StampPrintf("ixw:%lu", pGPS->_u8_ixw);
    StampPrintf("sol:%u", pGPS->_time_data._u8_is_solution_active);
    StampPrintf("unl:%lu", pGPS->_time_data._u32_utime_nmea_last);
    StampPrintf("snl:%llu", pGPS->_time_data._u64_sysclk_nmea_last);
    StampPrintf("age:%llu", u64_GPS_last_age_sec);
    StampPrintf("utm:%lu", u32_unixtime_now);
    StampPrintf("rmc:%lu", pGPS->_time_data._u32_nmea_gprmc_count);
    StampPrintf("pps:%llu", pGPS->_time_data._u64_sysclk_pps_last);
    StampPrintf("ppb:%lld", pGPS->_time_data._i32_freq_shift_ppb);
}

/// @brief Extracts maidenhead type QTH locator (such as KO85) using GPS coords.
/// @param pctx Ptr to WSPR beacon context.
/// @return ptr to string of QTH locator (static duration object inside get_mh).
/// @remark It uses third-party project https://github.com/sp6q/maidenhead .
char *WSPRbeaconGetLastQTHLocator(void)
{
    assert_(becaconData._pTX);
    assert_(becaconData._pTX->_p_oscillator);
    assert_(becaconData._pTX->_p_oscillator->_pGPStime);

    return get_mh(becaconData._pTX->_p_oscillator->_pGPStime->_time_data.lat, becaconData._pTX->_p_oscillator->_pGPStime->_time_data.lon, 8);
}

uint8_t WSPRbeaconIsGPSsolutionActive(void)
{

    assert_(becaconData._pTX);
    assert_(becaconData._pTX->_p_oscillator);
    assert_(becaconData._pTX->_p_oscillator->_pGPStime);

    return YES == becaconData._pTX->_p_oscillator->_pGPStime->_time_data._u8_is_solution_active;
}
