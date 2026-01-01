# pico-WSPR-tx
Modified version of WSPR beacon for Raspberry Pi Pico, based on pico-hf-oscillator PLL DCO library.
The GPS reference is used to compensate Pico's clock drift. GPS is also used to schedule WSPR transmissions.

![image](https://github.com/RPiks/pico-WSPR-tx/assets/47501785/3f835a9d-fa42-4eb8-ba93-ea72033d9e62)
The example of this WSPR beacon (QRPX power level) reception. Last max QRB is ~3400 km on 40m band.

![image](https://github.com/RPiks/pico-WSPR-tx/assets/47501785/a86280b9-71cb-4bb2-8b3c-0e33d2499aca)
High spectrum quality and less than 1Hz frequency drift.

# *NO* additional hardware
The WSPR beacon provides the output signal on the GPIO pin of Raspberry Pi Pico. No externall PLL, analog oscillators!
You should only provide a lowpass filter of appropriate frequency if you want to use this module extensively. 
The power transmitted using GPIO pin is sufficient only when using full-size dipole as antenna. If there is no such option available, you need to boost the signal using simple 1 transistor amplifier.

# For what?
This is an experimental project of amateur radio hobby and it is devised by me in order to experiment with QRP narrowband digital modes.
I am licensed radio amateur who is keen on experiments in the area of the digital modes on HF. 
My QRZ page is https://www.qrz.com/db/R2BDY

# Quick-start
1. Install Raspberry Pi Pico SDK. Configure environment variables. Test whether it is built successfully.

2. clone this repo
3. Open the repo folder in VSCode
4. When prompted, import folder as a Pico project
5. Compile and 'Run' the project to upload into the Pico.
6. Use a serial terminal to enter your callsign and location
7. Default band is 40m


Cheers,
Roman Piksaykin, amateur radio callsign R2BDY
https://www.qrz.com/db/R2BDY
piksaykin@gmail.com


Enhancements and modifications by VK3KYY December 2025

1. Add external button press to manually start Tx
2. Add settings configuation via serial terminal to allow callsign, location, band and other things to be set without needing to compile from source
3. Fixed problem of Tx not ending until the start of the next 'slot'
4. Fixed DT problem caused by modulation control ISR effectively free running
5. Changed memory allocation to be static as dymanic memory allocation is generally avoided in firmware applications
6. Automatic detection of GPS module if attached. Otherwise default to external button press
7. Hold external button when connecting Pico to USB forces settings entry so changes can be made
8. Currently removed the overclocking, as the firmware would not run on many Pico boards when overclocked. Note this theoretically limits the highest freq to around 10MHz, but higher freqs still seem to work.
9. Add frequency hoping option to settings
10. Added frequency calibration to settings for use when not connected to GPS that provides frequency calibration
11. Fixed bug with initial part of transmission was sending the last data 'symbol' from the last transmission
12. Improved LED flashing functionality. Slow very slow flash, waiting for external button to start. slow flash in passive slot, fast flash transmitting
13. Add setting to allow initial freq from the center of the WSPR range to be set.
14. Now works on both the original Pico (1) and also the Pico 2. Pico 2 works in both ARM and RISC-V mode
15. Added functionality to send 6 figure Maindenhead locator
16. Added functionality to use GPS to set the Maidenhead locator.
17. Added CW Beacon functionality
18. Added Slow Morse functionality which sends 5 figure letter / number groups


Still to do. 

1. Add option to overclock to higher frequencies. Specifically 270Mhz as in the original code, and higher than 270MHz to provide better RF generation
2. Implement multi-band option
3. Implement FT8
4. Implement APRS

KNOWN BUGS!
1. The firmware seems to hang / crash on frequencies above the 20MHz band when using the GPS.  
I checked the original code and it also does this. Hence the problem does not seem to be related to my changes
2. Overclocking has been reduced to 200MHz, which limits the maximum theoretical freq output to the 15m band, but it may still work above that freq.
The overclocking was reduced because the original 270MHz does not work all Pico boards, especially the Chinese clones 


Quickstart

1. Install the firmware
2. Open a serial monitor to the USB Serial from the Pico
3. Enter your callsign   e.g.  CALLSIGN VK3KYY  
4. Enter your Maidenhead locator e.g. LOCATOR QF12AB   .   By default only the first 4 characters will be used unless sending 6 characters is enabled
5. Choose the MODE.   WSPR , CW , SLOWMORSE .  Default will be WSPR.  FT8 and APRS , and other modes may be added at a later date.
6. Type Exit  or Reboot or power cycle the board

By default the firmware will check for 2 seconds if a GPS is connected. If GPS is set to OFF it won't check and GPS won't be used even if connected.
If a GPS is not connnected, or its disabled in the settings, the firmware will wait for a button attached to the board to be pressed.


GPS can be connected to
GPS_PPS  to PIN GPIO 2
GPS serial data to UART 0. On the UART Rx in on the Pico needs to be connected (GPIO 4)
GPS_PPS is necessary for GPS operation as it is used for accurate time keeping and also to calibrate the Tx freq and compensate for drift duing Tx.


RF Pin is configurable in the settings, and defaults to GPIO 6. I don't know if all GPIO pins work.

When not using GPS. The inaccuracy of the master clock crystal on the Pico board can be compensated for by using the CALPPM setting.
This value can be positive or negative and is the Parts Per Million amount of correction needed.
By default the WSPR beacon will transmit in the middle of the WSPR band, so using a receiver to check whether the transmission is on frequency, the PPM value can be calculated and entered.

A button need to be connected when not using a GPS for WSPR mode. Pressing the button starts the WSPR transmission
The button needs to be connected between GPIO 21 and Vcc (3.3V). An internal pulldown is used, so there is no need for any other external components
The button pin is not currently configurable

Holding the Button Pin when powering the Pico will force entry into the Settings


IMPORTANT

The RF output from the Pico is basically a square wave and hence contains lots of harmonics. 
A low pass filter should be connected between the RF pin and an antenna to prevent spurious unintended harmonics etc being transmitted.

Also. The frequency generation method seems to produce multiple artifacts aka Spur Frequencies, at regular intervals around the intended output frequency.
The frequency generation method in the original code is not documented, but is believed to be similar to that used in the Si5351
See this web page where the Si5351 frequency generation method is reverse engineered
https://www.pa3fwm.nl/technotes/tn42b-si5351-analysis.html

Hence this is not a pure continouos square wave of the output frequecy, it's a combination of 2 different square wave frequences which are transmitted for a minimum of 2 cycles.


Enjoy


73

Roger
VK3KYY

