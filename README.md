## Disassembly of the NIU N1S e-scooter ECU firmware

This is the beginning of a Disassembly of an probably V1.x ECU module from an NIU N1S e-scooter.

It basdically consists of am *STM32F103* (ARM Cortex M3) controller, a UBLOX *MAX-7Q* GPS module and a *M590E* GSM module from Neoway. 
I'm currently start commenting the STM32 code, identifying basic functions like `strcpy` or `atoi`.


