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


Still to do. 
1. Confirm whether 200MHz overclocking works with all Pico boards, as announced by the RPi foundation in 2025
2. Add option to overclock to higher frequencies. Specifically 270Mhz as in the original code, and higher than 270MHz to provide better RF generation
3. Implement multi-band option
4. Make code work on the Pico 2. Currently the direct use hardware IRQ's prevents the code compiling on the Pico 2, and is probably unnecessary and could be accumplished using higher level / portable SDK API functions
5. Implement transmission of 6 character locator
6. Check GPS -> Maindenhead functionality works and make it optional in the settings

73

Roger
VK3KYY

