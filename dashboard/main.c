/* MAIN.C file
 
 Alternative Firmware for NIU N1s (2017 model) electric scooter Dashboard.
 
 * Copyright (c) 2020 Axel Muhr
 
 TODO: Remove DBGdelay() in ht1621.c/h used for debugging AWU issues.
 
 */
 
#include <string.h>
#include "stm8s.h"
#include "stm8s_adc1.h"
#include "stm8s_awu.h"
#include "stm8s_itc.h"
#include "stm8s_gpio.h"
#include "ht1621.h"

#define PARK_ON()  GPIO_WriteHigh(GPIOE, GPIO_PIN_5)/* RED LED in Dashboard (i.e "PARK") */
#define PARK_OFF() GPIO_WriteLow(GPIOE, GPIO_PIN_5)

#define HIGH_ON()  GPIO_WriteHigh(GPIOE, GPIO_PIN_6)/* Blue LED in Dashboard (i.e "Highbeam") */
#define HIGH_OFF() GPIO_WriteLow(GPIOE, GPIO_PIN_6)

#define LEFT_ON()  GPIO_WriteHigh(GPIOB, GPIO_PIN_0)
#define LEFT_OFF() GPIO_WriteLow(GPIOB, GPIO_PIN_0)

#define BATT_ON()  GPIO_WriteHigh(GPIOB, GPIO_PIN_1)
#define BATT_OFF() GPIO_WriteLow(GPIOB, GPIO_PIN_1)

#define RIGHT_ON()  GPIO_WriteHigh(GPIOB, GPIO_PIN_2)
#define RIGHT_OFF() GPIO_WriteLow(GPIOB, GPIO_PIN_2)

#define BACKLIGHT_ON()  GPIO_WriteHigh(GPIOD, GPIO_PIN_2)
#define BACKLIGHT_OFF() GPIO_WriteLow(GPIOD, GPIO_PIN_2)

#define MAX485_WRITE()  GPIO_WriteHigh(GPIOC, GPIO_PIN_1)
#define MAX485_READ()   GPIO_WriteLow(GPIOC, GPIO_PIN_1)

#define LED_TOGGLE() GPIO_WriteReverse(GPIOE, GPIO_PIN_5)

/* --- UART Stuff --- */
#define UART_BUF_SIZE 128 // Read buffer 
uint8_t read_ok = 0; 			//read completion flag 
uint8_t read_idx = 0;
uint8_t read_len = 0;


/*
	--- LCD Stuff ---
	
	Char settings
	Adresses
	etc
	
*/

// Simulating the 32x4 LCD memory, i.e. 2 nibbles per int-line
// Due to misalignment in the LED segment-usage it's not possible to
// put two nibbles into one int
uint8_t LCD_ram[16]; 

#define getnibblelen(LCD_ram) (sizeof(LCD_ram) * 2) /* works because it's an ARRAY of uint8_t, wouldn't work if you're passing the array to a function. */
#define getnibble(LCD_ram, idx) (idx % 2 ? /* if odd */ LCD_ram[idx/2] & 0xf0 >> 4 : /* if even */ LCD_ram[idx/2] & 0x0f)
#define setnibble(LCD_ram, idx, val) (idx % 2 ? LCD_ram[idx/2] = (LCD_ram[idx/2] & 0x0f) | ((val & 0x0f) << 4) : LCD_ram[idx/2] = (LCD_ram[idx/2] & 0xf0) | (val & 0x0f))

 
const uint8_t digit[10] = {
  0xfa, //0
  0x60, //1
  0xd6, //2
  0xf4, //3
  0x6c, //4
  0xbc, //5
  0xbe, //6
  0xe0, //7
  0xfe, //8
  0xfc  //9
}; 

const uint8_t letter[] = {
	0xee, // A
	0x9e, // E
	0x6e, // H
	0x1a, // L
	0xea, // N
	0x7a  // U
};


//  Memory locations of digits by 32 nibbles
enum {
	speedo_2 = 1,
	speedo_1 = 3,
	mode = 4,
	odo_5 = 6,
	odo_4 = 8,
	odo_3 = 10,
	odo_2 = 12,
	odo_1 = 14,
	charge_2 = 16,
	charge_1 = 18,
	special = 19,		// just one nibble
	temp_2 = 21,
	temp_1 = 23,
	chrg_3 = 24,
	chrg_2 = 25,
	chrg_1 = 26,	// the 2 low-bits of this nibble are special signs
	amp_1 = 27,  	// each bit in this nibble represents 3 bars on the display
	amp_2 = 28,
	amp_3 = 29,
	amp_4 = 30
}; 

/* enum {
  speedo_2 = 0,
  speedo_1 = 1,
	mode = 2,
	odo_5 = 3,
	odo_4 = 4,
	odo_3 = 5,
	odo_2 = 6,
  odo_1 = 7,
	charge_2 = 8,
	charge_1 = 9,
	spec_temp2b = 10 		// !! From here the nibbles are unaligned !!
	temp_2a_temp1b = 11,
	temp_1a_chrg_3 = 12
	chrg_2_chrg_1 = 13, // the 2 low-bits of this nibble are special signs 			
	amp_2_1 = 14, 			// each bit in this nibble represents 3 bars on the display
	amp_4_3 = 15,
}; */


// Special signs

#define KMH 0x01 			// 1 - Always on
#define PERCENT 0x1 		// 3 - Always on
#define MODE_SIGN 0x8		// 4 - Always on
#define KM 0x01 			// 6 - Always on
#define AMPERE 0x01 		// 16 - Always on
#define BARS 0x1 			// 19 - Always on
#define REVERSE 0x2 		// 19
#define ECO 0x4 			// 19
#define CELSIUS_SIGN 0x01 	// addr 21 - Allways on
#define MINUS_CELSIUS 0x01	// 23
#define OVERHEAT 0x01 		// 26
#define PLUG 0x2 			// 26

/* --- Prototypes ---*/

void delay(unsigned int);
void ADC_init(void);
void AWU_setup(void);
static void CLK_init(void);
void GPIO_init(void);
void UART_init(void);
void uart_read_n_byte(uint8_t*, uint8_t);
@near uint8_t read_buffer[UART_BUF_SIZE]; // @near can be placed when the buffer setting is large
@far @interrupt void UART2_RX_IRQHandler(void);
void write_ram(uint8_t addr,uint8_t sdata);
void write_digits(short value, uint8_t type);
void update_lcd(void);

void main(void) {

	uint16_t adc_res; 	/* The latest ADC result is stored here. */
	HT1621Values Seg_LCD; /* Caution! No variable definitions after first instruction. */
	int counter;
  
	CLK_init(); /* Initialize the clock. */
	UART_init();
	ADC_init();
	GPIO_init();
	AWU_setup();  /* Initilize the AWU peripheral. */
	
	ADC1_Cmd(ENABLE); //ADC_CR1_ADON = 1;
	
	/* DISPLAY STUFF */
	 
	HT1621_PortInit();  /* Processor port settings for the display. */
	HT1621_Init();      /* Init the display. */
	BACKLIGHT_ON();
  // HT1621_ValuesConstructor(&Seg_LCD); /* Initialize LCD character (digit) map.*/

  // HT1621_AllOn(16); /*Turn on all segments. */
	//AWU_Init(AWU_TIMEBASE_1S); //Set alarm time to 1 second later.
  // halt(); //Sleep.
	
	HT1621_AllOff(16); // Clear display
	
	
  while(1){ 

    //This is an ADC conversion demo.
    // See commented out code below for some other examples.
    
    /* ADC1_StartConversion(); //TODO: Check why twice ADON is necessary.
    ADC1_StartConversion(); //
    
    while(ADC1_GetFlagStatus(ADC1_FLAG_EOC)==0); //Wait until end of conversion.
    
    adc_res=ADC1_GetConversionValue(); //Read the result.
    ADC1_ClearFlag(ADC1_FLAG_EOC); //This does not seem to be necessary...
    
    
    Seg_LCD.decimalPt=4; //Equivalent to hiding the point. See HT1621.c

    //Do the ritual to convert data to LCD segment display:
    HT1621_Convert(adc_res,&Seg_LCD);
    HT1621_Blanking(&Seg_LCD);  //Remove leading zeros.
    // Write the data prepared above, at once.
    HT1621_Refresh (&Seg_LCD); */

		
		HT1621_Write(speedo_2, digit[2]); // Speedo 2nd digit
		HT1621_Write(speedo_1, digit[1]); // 1st digit
		
		HT1621_Write(odo_5, digit[0]); // O
		HT1621_Write(odo_4, 0x1a); // L
		HT1621_Write(10, 0x1a); // L
		HT1621_Write(12, 0x9e); // E
		HT1621_Write(14, 0x6e); // H

    RIGHT_ON();

		delay(64000);
		BATT_ON();
		delay(64000);
		PARK_ON();
		delay(64000);
		HIGH_ON();
		delay(64000);
		LEFT_ON();
		delay(64000);
		
		HT1621_AllOff(16);
			
		HT1621_Write(speedo_2, digit[4]); // Speedo 2nd digit
		HT1621_Write(speedo_1, digit[3]); // 1st digit	

		HT1621_Write(8, 0x7a); // U
		HT1621_Write(10, digit[1]); // I
		HT1621_Write(12, 0xea); // N
	
		
    RIGHT_OFF();
		
		delay(64000);
		BATT_OFF();
		delay(64000);
		PARK_OFF();
		delay(64000);
		HIGH_OFF();
		delay(64000);
		LEFT_OFF();
		delay(64000);		
	

	/* HT1621_OneByOne(31);
	// HT1621_Write(0, 0xf);
		
	AWU_Init(AWU_TIMEBASE_1S); //was 128MS - Repeat after sleeping 128ms.
    halt();
		
  	AWU_Init(AWU_TIMEBASE_1S); //was 128MS - Repeat after sleeping 128ms.
    halt(); 
	HT1621_AllOff(16);*/
  }


}

/* void AWU_ISR(void) __interrupt(AWU_INT)
//SDCC says that ISRs must reside within the main code file.
{
  AWU_GetFlagStatus();  //Clear AWU peripheral pending bit
}
*/

void delay(unsigned int n)
{
    while (n-- > 0);
}

void GPIO_init(void)
{
	/* Init the LED Pins 19-32 */
	GPIO_DeInit(GPIOE);
	GPIO_Init(GPIOE, ((GPIO_Pin_TypeDef)(GPIO_PIN_5 | GPIO_PIN_6)), GPIO_MODE_OUT_PP_LOW_SLOW);
	
	GPIO_DeInit(GPIOB);
	GPIO_Init(GPIOB, ((GPIO_Pin_TypeDef)(GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2)), GPIO_MODE_OUT_PP_LOW_SLOW);
	GPIO_Init(GPIOB, GPIO_PIN_4, GPIO_MODE_IN_FL_NO_IT); /* ADC1, CH2: PB4 */
	
	GPIO_Init(GPIOD, ((GPIO_Pin_TypeDef)(GPIO_PIN_2)), GPIO_MODE_OUT_PP_LOW_SLOW); // Background light
	
	GPIO_Init(GPIOC, ((GPIO_Pin_TypeDef)(GPIO_PIN_1)), GPIO_MODE_OUT_PP_LOW_SLOW); // Direction ctrl for max485
	// GPIOs for HT1621 are set in ht1621.h & .c
	
}

//-------------------------------------------------------------------------
// Setup ADC1 to perform a single conversion and then generate an interrupt.
void ADC_init(void)
{  
	ADC1_DeInit();
	
	ADC1_Init(ADC1_CONVERSIONMODE_SINGLE, ADC1_CHANNEL_2,     \
			ADC1_PRESSEL_FCPU_D2,                           \
			ADC1_EXTTRIG_TIM, DISABLE,                      \
			ADC1_ALIGN_RIGHT, ADC1_SCHMITTTRIG_CHANNEL2,    \
			DISABLE);
	
	ADC1_ITConfig(ADC1_IT_EOCIE, DISABLE);  // Disable EOC interrupt.
	// For the first argument, see stm8_adc1.h; ADC1_IT_TypeDef
}

/**
  * @brief  Configure system clock to run at 16Mhz
  * @param  None
  * @retval None
  */
static void CLK_init(void)
{
    /* Initialization of the clock */
    /* Clock divider to HSI/1 */
    // CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV1);
	CLK_DeInit();
	CLK_HSECmd(DISABLE);
	CLK_LSICmd(DISABLE);
	CLK_HSICmd(ENABLE);
	while(CLK_GetFlagStatus(CLK_FLAG_HSIRDY) == FALSE);
	CLK_ClockSwitchCmd(ENABLE);
	CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV4);
	CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV1);
	CLK_ClockSwitchConfig(CLK_SWITCHMODE_AUTO, CLK_SOURCE_HSI,DISABLE, CLK_CURRENTCLOCKSTATE_ENABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_I2C, DISABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_UART1, ENABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_ADC, ENABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_SPI, DISABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_AWU, ENABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_TIMER1, DISABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_TIMER2, DISABLE);
	CLK_PeripheralClockConfig(CLK_PERIPHERAL_TIMER4, DISABLE);
}

void AWU_setup(void)
{
    /* Initialization of AWU - can not named _init due to name collision */
		
	AWU_IdleModeEnable();    
	AWU_DeInit();
	AWU_LSICalibrationConfig(128000);
	AWU_Init(AWU_TIMEBASE_2S);
	AWU_Cmd(ENABLE);
	enableInterrupts();
}

void UART_init(void)
{
	 // Serial port parameters should be modified as required
	UART2_DeInit();
	UART2_Init((uint32_t)38400, 
	UART2_WORDLENGTH_8D, 
	UART2_STOPBITS_1, 
	UART2_PARITY_NO, 
	UART2_SYNCMODE_CLOCK_DISABLE, 
	UART2_MODE_TXRX_ENABLE);
			
	 // Explicitly close the interrupt (default is off)
	 // Note:
	 // The read interrupt name is UART2_IT_RXNE_OR instead of UART2_IT_RXNE
	 // Write the interrupt name to UART2_IT_TXE
	UART2_ITConfig(UART2_IT_RXNE_OR, DISABLE);
	UART2_ITConfig(UART2_IT_TXE, DISABLE);
	
	 // serial port enable
	UART2_Cmd(ENABLE);
}

void write_ram(uint8_t addr,uint8_t sdata) {

	LCD_ram[addr] == sdata;

}

/* void write_bars(uint8_t value, uint8t type) {
		temp_1a_chrg_3 = 12
	chrg_2_chrg_1 = 13, // the 2 low-bits of this nibble are special signs 			
	amp_2_1 = 14, 			// each bit in this nibble represents 3 bars on the display
	amp_4_3 = 15,
} */
	
void write_digits(short value, uint8_t type) {
	
uint8_t addr;
int position[5]; 
int i = 0;

while(value > 0) {
   position[i]= value % 10;   // to get the right most digit
   value /= 10;              //reduce the number by one digit
   ++i;
}


// type: 0=speedo, 1=odo, 2=charge, 3=temp

if (type < 4) {
	switch (type) {
		case 0:
			HT1621_Write(speedo_2, digit[position[1]]); // Tacho 2. stelle
			HT1621_Write(speedo_1, digit[position[0]]); // 1. stelle
			break;
		case 1:
			HT1621_Write(odo_5, digit[position[4]]); // Tacho 5. stelle
			HT1621_Write(odo_4, digit[position[3]]); // Tacho 4. stelle
			HT1621_Write(odo_3, digit[position[2]]); // Tacho 3. stelle
			HT1621_Write(odo_2, digit[position[1]]); // Tacho 2. stelle
			HT1621_Write(odo_1, digit[position[0]]); // 1. stelle
			break;
		case 2:
			HT1621_Write(charge_2, digit[position[1]]); // Tacho 2. stelle
			HT1621_Write(charge_1, digit[position[0]]); // 1. stelle
			break;
		case 4:
			HT1621_Write(temp_2, digit[position[1]]); // Tacho 2. stelle
			HT1621_Write(temp_1, digit[position[0]]); // 1. stelle
			break;
	}
	LCD_ram[addr] == value;
} else { // temp needs special handling
	
}


}
/*
void update_lcd(void) {

// We're "stamping" the basic display items here

	LCD_ram[addr] == LCD_ram[addr & 0x1];
#define KMH 0x01 			// 1 - Allways on
#define PERCENT 0x1 	// 3 - Allways on
#define MODE_SIGN 0x8	// 4 - Allways on
#define KM 0x01 			// 6 - Allways on
#define AMPERE 0x01 	// 16 - Allways on
#define BARS 0x1 			// 19 - Allways on

#define CELSIUS_SIGN 0x01 // addr 21 - Allways on

	// and push it onto the diplay
	HT1621_WriteRam(LCD_ram[]);

}*/

 // read multiple bytes
void  uart_read_n_byte(uint8_t* data, uint8_t len)
{
	 // turn off interrupt
	UART2_ITConfig(UART2_IT_RXNE_OR, DISABLE);

	 // Clear the read buffer (reset the read index value)
	read_idx = 0;
	read_len = len;

	 // open read interrupt
	UART2_ITConfig(UART2_IT_RXNE_OR, ENABLE);
	// Wait for the read operation to complete (synchronization processing), add timeout processing, refer to the above write operation
	 while(!read_ok); 
	 // write complete, reset write complete flag
	 read_ok = 0; 
	 memcpy(data, read_buffer, read_len); // copy data to user buffer
	return;
}

/* -- We're not wrting to the CAN bus --

@far @interrupt void UART2_TX_IRQHandler(void) //, 20)
{
	 // Write operation automatically clear the interrupt, so you can not explicitly clear the interrupt
	//UART2_ClearITPendingBit(UART2_IT_TXE); 
	
	 // Write 1 byte from the write buffer
	UART2_SendData8(writ_buffer[writ_idx++]);

	 // All write, write off interrupt, write completion flag (synchronization processing)
	if( writ_idx == writ_len ) {
		UART2_ITConfig(UART2_IT_TXE, DISABLE);
		writ_ok = 1;
	}
}
*/


@far @interrupt void UART2_RX_IRQHandler(void) //, 21)
{
	 // The read operation automatically clears the interrupt, so there is no need to explicitly clear the interrupt.
	 // Note that the interrupt name here is RXNE, not RXNE_OR
	UART2_ClearITPendingBit(UART2_IT_RXNE);

	 // read 1 byte
	read_buffer[read_idx++] = UART2_ReceiveData8();

	 // Read all, turn off the interrupt (UART2_IT_RXNE_OR), read completion flag (synchronization processing)
	if( read_idx == read_len ) {
		UART2_ITConfig(UART2_IT_RXNE_OR, DISABLE);
		read_ok = 1;
	}
} 
