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

int lastOffsetFreq = 0;

/// @brief Initializes a new WSPR beacon context.
/// @param pcallsign HAM radio callsign, 12 chr max.
/// @param pgridsquare Maidenhead locator, 7 chr max.
/// @param txpow_dbm TX power, db`mW.
/// @param pdco Ptr to working DCO.
/// @param dial_freq_hz The begin of working WSPR passband.
/// @param shift_freq_hz The shift of tx freq. relative to dial_freq_hz.
/// @param gpio Pico's GPIO pin of RF output.
/// @return Ptr to the new context.
WSPRbeaconContext *WSPRbeaconInit(const char *pcallsign, const char *pgridsquare, int txpow_dbm,
                                  PioDco *pdco, uint32_t dial_freq_hz, int32_t shift_freq_hz,
                                  int gpio)
{
    assert_(pcallsign);
    assert_(pgridsquare);
    assert_(pdco);

    WSPRbeaconContext *p = calloc(1, sizeof(WSPRbeaconContext));
    assert_(p);

    strncpy(p->_pu8_callsign, pcallsign, sizeof(p->_pu8_callsign));
    strncpy(p->_pu8_locator, pgridsquare, sizeof(p->_pu8_locator));
    p->_u8_txpower = txpow_dbm;

    p->_pTX = TxChannelInit(682667, 0, pdco);
    assert_(p->_pTX);

    TxChannelSetFrequency(dial_freq_hz, shift_freq_hz);

    p->_pTX->_i_tx_gpio = gpio;

    return p;
}

/// @brief Sets dial (baseband minima) freq.
/// @param pctx Context.
/// @param freq_hz the freq., Hz.
void WSPRbeaconSetDialFreq(WSPRbeaconContext *pctx, uint32_t freq_hz)
{
    assert_(pctx);
    pctx->_pTX->_u32_Txfreqhz = freq_hz;
}

/// @brief Constructs a new WSPR packet using the data available.
/// @param pctx Context
/// @return 0 if OK.
int WSPRbeaconCreatePacket(WSPRbeaconContext *pctx)
{
    assert_(pctx);

    wspr_encode(pctx->_pu8_callsign, pctx->_pu8_locator, pctx->_u8_txpower, pctx->_pu8_outbuf);

    return 0;
}

/// @brief Sends a prepared WSPR packet using TxChannel.
/// @param pctx Context.
/// @return 0, if OK.
int WSPRbeaconSendPacket(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);
    assert_(pctx->_pTX->_u32_Txfreqhz > 500 * kHz);

    TxChannelClear(pctx->_pTX);

    memcpy(pctx->_pTX->_pbyte_buffer, pctx->_pu8_outbuf, WSPR_SYMBOL_COUNT);
    pctx->_pTX->_ix_input = WSPR_SYMBOL_COUNT;



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

int WSPRbeaconTxScheduler(WSPRbeaconContext *pctx, uint32_t initSlotOffset, int verbose)
{
    assert_(pctx);
    bool debugPrint = verbose;

    datetime_t rtcDateTime;
    uint32_t isec_of_hour;
    uint32_t islot_number;
    uint32_t islot_modulo;

    if( pctx->_txSched._u8_tx_GPS_mandatory)
    {
        // PPS occurs at the start of the second before the RMC message is received, hence the actual time at PPS is + 1 second from the last nmea time
        isec_of_hour = (pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + 1) % HOUR;
    }
    else
    {
        rtc_get_datetime(&rtcDateTime);
        isec_of_hour = rtcDateTime.min * 60 + rtcDateTime.sec;       
    }

    islot_number = (isec_of_hour  / (2 * MINUTE)) + initSlotOffset;
    islot_modulo = islot_number % pctx->_txSched._u8_tx_slot_skip;

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

                WSPRbeaconSendPacket(pctx);
               
                printf("WSPR> Start TX.\n");
            }
        }
        else
        {
            // Check if Tx has finished and Osc has been turned off
            if (!pctx->_pTX->_p_oscillator->_is_enabled)
            {
                printf("WSPR> End Tx. @ %d secs\n",secsIntoCurrentSlot);

                if (settingsData.frequencyHop)
                {
                    // Set the freq of the next transmission now.
                    const int FREQ_STEP_SIZE = 5;// Hz
                    const int rangeInHzSteps = WSPR_FREQ_RANGE_HZ / FREQ_STEP_SIZE;
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
void WSPRbeaconDumpContext(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);

    const uint64_t u64tmnow = GetUptime64();
    const uint64_t u64_GPS_last_age_sec 
        = (u64tmnow - pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u64_sysclk_nmea_last) / 1000000ULL;

    StampPrintf("__________________");
    StampPrintf("=TxChannelContext=");
    StampPrintf("ftc:%llu", pctx->_pTX->_tm_future_call);
    StampPrintf("ixi:%u", pctx->_pTX->_ix_input);
    StampPrintf("dfq:%lu", pctx->_pTX->_u32_Txfreqhz);
    StampPrintf("gpo:%u", pctx->_pTX->_i_tx_gpio);

    GPStimeContext *pGPS = pctx->_pTX->_p_oscillator->_pGPStime;
    const uint32_t u32_unixtime_now 
            = pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u32_utime_nmea_last + u64_GPS_last_age_sec;
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
char *WSPRbeaconGetLastQTHLocator(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);
    assert_(pctx->_pTX->_p_oscillator);
    assert_(pctx->_pTX->_p_oscillator->_pGPStime);
    
    const double lat = 1e-5 * (double)pctx->_pTX->_p_oscillator->_pGPStime->_time_data._i64_lat_100k;
    const double lon = 1e-5 * (double)pctx->_pTX->_p_oscillator->_pGPStime->_time_data._i64_lon_100k;

    return get_mh(lat, lon, 8);
}

uint8_t WSPRbeaconIsGPSsolutionActive(const WSPRbeaconContext *pctx)
{
    assert_(pctx);
    assert_(pctx->_pTX);
    assert_(pctx->_pTX->_p_oscillator);
    assert_(pctx->_pTX->_p_oscillator->_pGPStime);

    return YES == pctx->_pTX->_p_oscillator->_pGPStime->_time_data._u8_is_solution_active;
}
