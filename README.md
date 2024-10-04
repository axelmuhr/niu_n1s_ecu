## Disassembly of the NIU N1S e-scooter ECU firmware

This is the beginning of a Disassembly of an probably V1.x ECU module from an NIU N1S e-scooter.

It basdically consists of am *STM32F103* (ARM Cortex M3) controller, a UBLOX *MAX-7Q* GPS module and a *M590E* GSM module from Neoway. 
I'm currently start commenting the STM32 code, identifying basic functions like `strcpy` or `atoi`.

Additionally, you'll find the dump of the 512k SPI flash ROM. I *guess* that's being used for saving a new firmware before flashing it into the STM32's internal flash. 
I conclude this as I found (probably) the same ARM code in there as in my disassembly. I wrote 'Probably' because I did no yet made a diff between those two - if it's an updated firmware, it should
differ though.


