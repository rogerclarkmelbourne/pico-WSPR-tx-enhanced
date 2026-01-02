#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <piodco.h>
#include <TxChannel.h>
#include "persistentStorage.h"

uint32_t CW_SYMBOL_LIST[] =
{
0x00000000,//Space
0x00000000,// ! Not encoded. 
0x00000000,// " Not encoded. 
0x00000000,// # Not encoded. 
0x00000000,// $ Not encoded. 
0x00000000,// % Not encoded. 
0x00000000,// & Not encoded. 
0x00000000,// ' Not encoded. 
0x00000000,// ( Not encoded. 
0x00000000,// ) Not encoded. 
0x00000000,// * Not encoded. 
0x00000000,// + Not encoded. 
0x00077577,// , (comma)
0x00007557,// - (dash). 
0x0001d75d,// .  (full stop)
0x00001757,// /  (forward slash)
0x00077777,//0
0x0001dddd,//1
0x00007775,//2
0x00001dd5,//3
0x00000755,//4
0x00000155,//5
0x00000557,//6
0x00001577,//7
0x00005777,//8
0x00017777,//9
0x00000000,// : Not encoded.
0x00000000,// ; Not encoded.
0x00000000,// < Not encoded.
0x00001d57,// =
0x00000000,// > Not encoded.
0x00000000,// ? Not encoded.
0x00000000,// @ Not encoded.
0x0000001d,// A
0x00000157,// B
0x000005d7,// C
0x00000057,// D
0x00000001,// E
0x00000175,// F
0x00000177,// G
0x00000055,// H
0x00000005,// I
0x00001ddd,// J
0x000001d7,// K
0x0000015d,// L
0x00000077,// M
0x00000017,// N
0x00000777,// O
0x000005dd,// P
0x00001d77,// Q
0x0000005d,// R
0x00000015,// S
0x00000007,// T
0x00000075,// U
0x000001d5,// V
0x000001dd,// W
0x00000757,// X
0x00001dd7,//Z
};

TxChannelContext *pTX;

static char cwMessage[32];
static const char* messagePtr;
static uint32_t bitPattern;
static int bitPatternCounter;
int cwMessageState = 0;
static int charIndex;
static const int BIT_COUNTER_RESET_VALUE = 4;

void sendMessageInitBitPattern(void)
{
	charIndex = *messagePtr - ' ';
	bitPattern = CW_SYMBOL_LIST[charIndex];
	cwMessageState = 1;
	bitPatternCounter = BIT_COUNTER_RESET_VALUE;// assume its a space character
}


bool sendMessageProgress(void)
{
	bool retVal = TRUE;
	switch (cwMessageState)
	{
		case 0:
			sendMessageInitBitPattern();
			// fallthrough
		case 1:
			if (bitPatternCounter > 0)
			{
				printf("%d", bitPattern & 0x01);
				if ((bitPattern & 0x01))
				{
				    PioDCOStart(pTX->_p_oscillator);// turn on the oscillator
					bitPatternCounter = BIT_COUNTER_RESET_VALUE;
				}
				else
				{
				    PioDCOStop(pTX->_p_oscillator);// turn off the oscillator
				}
				bitPatternCounter--;
				bitPattern = bitPattern >> 1;

			}
			else
			{

				messagePtr++;
				if (*messagePtr)
				{
					cwMessageState = 0;
				}
				else
				{
					retVal = false;
				}
			}

			break;
	}
	return retVal;
}

char randChar(void)
{
	const char *randCharStr="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	return randCharStr[rand() % 36];
}

void sendCwMessage(void)
{
	messagePtr = cwMessage;
	cwMessageState = 0;
	bool b;

	printf("Send message CW %s\n",messagePtr);
	do
	{
		b = sendMessageProgress();
		sleep_ms(1200 / settingsData.cwSpeed);
	} while (b);

	sleep_ms(1000);// wait 1 second
}

void handleCW(void)
{

	pTX = TxChannelInit(1000, 0);// 682667 is WSPR symbol duration in microseconds
	TxChannelSetFrequency(settingsData.txFreq,0);

	while(true)
	{

		snprintf(cwMessage,32, "%s",settingsData.callsign);
		sendCwMessage();// send callsign

		// Send Callsign + Locator or 5 fig group,    5 times
		for(int loopCounter = 0; loopCounter<5; loopCounter++)
		{

			if (settingsData.mode == MODE_SLOW_MORSE)
			{
				for(int i=0;i<5;i++)
				{
					cwMessage[i]=randChar();
				}
				cwMessage[5] = 0;// terminate
			}
			else
			{
				snprintf(cwMessage,32, "%s %s     ",settingsData.callsign,settingsData.locator);
			}

			sendCwMessage();

		}


		if (settingsData.mode == MODE_CW_BEACON)
		{
			sleep_ms(30 * 1000);// wait 30 seconds before retransmitting
		}
		else
		{
			sleep_ms(2 * 1000);// wait 30 seconds before retransmitting
		}
	}
}