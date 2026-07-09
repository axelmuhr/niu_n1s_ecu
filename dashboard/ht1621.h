
#ifndef HT1621_HEADER_H
#define HT1621_HEADER_H

#include "stm8s.h"
#include "stm8s_gpio.h"

#define TRUE 1
#define FALSE 0

#define HT1621Port GPIOC
#define HT1621Data GPIO_PIN_5 // Data:  PA1 => Output
#define HT1621WR   GPIO_PIN_3 // Write: PA3 => Output
#define HT1621CS   GPIO_PIN_2 // CS:    PA2 => Output
#define HT1621Signal_1(pin) {GPIO_WriteHigh(HT1621Port, pin);}
#define HT1621Signal_0(pin) {GPIO_WriteLow(HT1621Port, pin);}


//Set or clear the signal pins:
#define CS1    HT1621Signal_1(HT1621CS)
#define CS0    HT1621Signal_0(HT1621CS)
#define WR1    HT1621Signal_1(HT1621WR)
#define WR0    HT1621Signal_0(HT1621WR)
#define DATA1  HT1621Signal_1(HT1621Data)
#define DATA0  HT1621Signal_0(HT1621Data)

#define sbi(x, y)  (x |= (1 << y))   //Set bit i
#define cbi(x, y)  (x &= ~(1 <<y ))  //Clear bit i

//Commands specific to this LCD:
#define  ComMode    0x52  //4commons, 1/3bias  1000 010 10 0 1 0  
#define  RCosc      0x30  //Clock set: On chip RC 1000 0011 0000 
#define  LCD_on     0x06  //Turn on  LCD 1000 0000 0110 
#define  LCD_off    0x04  //Turn off LCD 1000 0000 0100
#define  Sys_en     0x02  //Turn oscil.on 1000 0000 0010 
#define  Sts_dis    0x00  //Turn oscil.off 1000 0000 0000 AO!
#define  CTRl_cmd   0x80  //Next byte is a command.
#define  Data_cmd   0xa0  //Next byte is a data

typedef struct {
//Define the character (digit) to LCD memory address mapping
// For example, to display '0' we must write 0x7D :
/*0,1,2,3,4,5,6,7,8,9,A,b,C,c,d,E,F,H,h,L,n,N,o,P,r,t,U,-, ,*/
  uint8_t map[28];//={0x7D,0x60,0x3E,0x7A,0x63,0x5B,0x5F,0x70,0x7F,0x7B,0x77,0x4F,0x1D,0x0E,0x6E,0x1F,0x17,0x67,0x47,0x0D,0x46,0x75,0x37,0x06,0x0F,0x6D,0x02,0x00,};

  uint8_t digit[6]; //Individual digits on the display.
  uint8_t decimalPt; //Location of the decimal point.
  uint8_t bat1,bat2,bat3;
}HT1621Values;

void HT1621_SendBits(uint8_t sdata,uint8_t cnt);
void HT1621_SendCmd(uint8_t command);
void HT1621_Write(uint8_t addr,uint8_t sdata);
void HT1621_WriteRam(uint8_t sdata[]);
void HT1621_AllOff(uint8_t num);
void HT1621_AllOn(uint8_t num);
void HT1621_Init(void);
void HT1621_Refresh(HT1621Values * LCD_data);
void HT1621_ValuesConstructor(HT1621Values * LCD_data);
void HT1621_Convert (long number, HT1621Values * LCD_data);
void HT1621_Blanking (HT1621Values * LCD_data);
void HT1621_PortInit(void);

void DBGdelay(unsigned int);
void HT1621_OneByOne(uint8_t num);


#endif