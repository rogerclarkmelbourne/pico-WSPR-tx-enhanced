///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  TxChannel.c - Produces a time-accurate `bit` stream.
//                Invokes a `modulator` function. 
//  DESCRIPTION
//      Receives data asynchronously. Calls low level modulator function
//      synchronously according to params.
//
//  HOWTOSTART
//      -
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
//      -
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
#include "TxChannel.h"

static TxChannelContext txChannelContext = {0};
static TxChannelContext *spTX = &txChannelContext;

PioDco DCO = {0};

/*
#ifdef PICO_RP2040
    #define BARE_METAL_TIMER
#endif
*/


#ifdef BARE_METAL_TIMER
static void __not_in_flash_func (TxChannelISR)(void)
#else
static int64_t __not_in_flash_func (TxChannelISR)(alarm_id_t id, void *user_data)
#endif
{
    PioDco *pDCO = txChannelContext._p_oscillator;

    uint8_t byte;
    const int n2send = TxChannelPop(spTX, &byte);
    if(n2send)
    {
        const int32_t i32_compensation_millis = 
            PioDCOGetFreqShiftMilliHertz(txChannelContext._p_oscillator, 
                                         (uint64_t)(txChannelContext._u32_Txfreqhz * 1000LL));

        PioDCOSetFreq(pDCO, txChannelContext._u32_Txfreqhz, 
                      (uint32_t)byte * WSPR_FREQ_STEP_MILHZ - 2 * i32_compensation_millis);


#ifdef BARE_METAL_TIMER
        txChannelContext._tm_future_call += txChannelContext._bit_period_us;
        hw_clear_bits(&timer_hw->intr, 1U<<txChannelContext._timer_alarm_num);
        timer_hw->alarm[txChannelContext._timer_alarm_num] = (uint32_t)txChannelContext._tm_future_call;
#endif        
    }
    else
    {
        TxChannelStop();
#ifndef BARE_METAL_TIMER
        return 0;// Timer should already be stopped, but returning 0 is also supposed to stop the timer alarm
#endif
    }
#ifndef BARE_METAL_TIMER
    return txChannelContext._bit_period_us;// next period duration
#endif 
}

/// @brief Initializes a TxChannel context. Starts ISR.
/// @param bit_period_us Period of data bits, BPS speed = 1e6/bit_period_us.
/// @param timer_alarm_num Pico-specific hardware timer resource id.
/// @param pDCO Ptr to oscillator.
/// @return the Context.
TxChannelContext * TxChannelInit(const uint32_t bit_period_us, uint8_t timer_alarm_num)
{
    assert_(bit_period_us > 10);


    txChannelContext._bit_period_us = bit_period_us;
    txChannelContext._timer_alarm_num = timer_alarm_num;
    txChannelContext._p_oscillator = &DCO;

    txChannelContext.alarmPool = alarm_pool_create_with_unused_hardware_alarm(1);
 

#ifdef BARE_METAL_TIMER
    hw_set_bits(&timer_hw->inte, 1U << txChannelContext._timer_alarm_num);
    irq_set_exclusive_handler(TIMER_IRQ_0, TxChannelISR);
    irq_set_priority(TIMER_IRQ_0, 0x00);
#endif    

    return &txChannelContext;
}

void TxChannelSetFrequency(uint32_t dialFreq, uint32_t offsetFreq)
{
    txChannelContext._u32_dialfreqhz = dialFreq;
    txChannelContext._u32_offsetfreqhz = offsetFreq;
    txChannelContext._u32_Txfreqhz =  txChannelContext._u32_dialfreqhz + (WSPR_FREQ_RANGE_HZ / 2) + txChannelContext._u32_offsetfreqhz;// set Tx freq to the middle of the WSPR Tx range +/- the offset
    PioDCOSetFreq(txChannelContext._p_oscillator, txChannelContext._u32_Txfreqhz, 0);// Reset the freq.

}

void TxChannelSetOffsetFrequency(uint32_t offsetFreq)
{
    txChannelContext._u32_offsetfreqhz = offsetFreq;
    txChannelContext._u32_Txfreqhz =  txChannelContext._u32_dialfreqhz + (WSPR_FREQ_RANGE_HZ / 2) + txChannelContext._u32_offsetfreqhz;// set Tx freq to the middle of the WSPR Tx range +/- the offset
    PioDCOSetFreq(txChannelContext._p_oscillator, txChannelContext._u32_Txfreqhz, 0);// Reset the freq.
}

void TxChannelStart(void)
{    

    PioDCOStart(txChannelContext._p_oscillator);// turn on the oscillator
#ifdef BARE_METAL_TIMER
    irq_set_enabled(TIMER_IRQ_0, true);
    txChannelContext._tm_future_call = timer_hw->timerawl;// + 140000UL;// VK3KYY Not sure why a delay is needed before the first symbol is transmitted
    timer_hw->alarm[txChannelContext._timer_alarm_num] = (uint32_t)txChannelContext._tm_future_call;
    TxChannelISR();
#else
	txChannelContext.alarmId = alarm_pool_add_alarm_in_us( txChannelContext.alarmPool, txChannelContext._bit_period_us, TxChannelISR, NULL, true );
    TxChannelISR(txChannelContext.alarmId, NULL);
#endif


}

void TxChannelStop(void)
{   
    PioDCOStop(txChannelContext._p_oscillator); // Turn off the oscillator

#ifdef BARE_METAL_TIMER    
    // Stop sending data.
    irq_set_enabled(TIMER_IRQ_0, false);
    timer_hw->alarm[txChannelContext._timer_alarm_num] = 0;    // Disable ALARM0 so it doesn't trigger
#else
    alarm_pool_cancel_alarm(txChannelContext.alarmPool, txChannelContext.alarmId);
#endif    
    PioDCOSetFreq(txChannelContext._p_oscillator, txChannelContext._u32_Txfreqhz, 0);// Reset the freq.
    gpio_put(PICO_DEFAULT_LED_PIN, 0); // Turn off the LED
}

/// @brief Gets a count of bytes to send.
/// @param pctx Context.
/// @return A count of bytes.
uint8_t TxChannelPending(TxChannelContext *pctx)
{
    return 256L + (int)pctx->_ix_input - (int)pctx->_ix_output;
}

/// @brief Push a number of bytes to the output FIFO.
/// @param pctx Context.
/// @param psrc Ptr to buffer to send.
/// @param n A count of bytes to send.
/// @return A count of bytes has been sent (might be lower than n).
int TxChannelPush(TxChannelContext *pctx, uint8_t *psrc, int n)
{
    uint8_t *pdst = pctx->_pbyte_buffer;
    while(n-- && pctx->_ix_input != pctx->_ix_output)
    {
        pdst[pctx->_ix_input++] = *psrc++;
    }

    return n;
}

/// @brief Retrieves a next byte from FIFO.
/// @param pctx Context.
/// @param pdst Ptr to write a byte.
/// @return 1 if a byte has been retrived, or 0.
int TxChannelPop(TxChannelContext *pctx, uint8_t *pdst)
{
    if(pctx->_ix_input != pctx->_ix_output)
    {
        *pdst = pctx->_pbyte_buffer[pctx->_ix_output++];

        return 1;
    }

    return 0;
}

/// @brief Clears FIFO completely. Sets write&read indexes to 0.
/// @param pctx Context.
void TxChannelClear(TxChannelContext *pctx)
{
    pctx->_ix_input = pctx->_ix_output = 0;
}
