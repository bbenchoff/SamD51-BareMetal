#include "sam.h"
#include <stdio.h>

#include "utils/print.h"
#include "utils/delay.h"

#define CONF_SERCOM_2_USART_BAUD_RATE 9600

#define CONF_SERCOM_0_USART_BAUD_RATE_REGISTER_VAL \
(65536 - ((65536 * 16.0f * (CONF_SERCOM_2_USART_BAUD_RATE)) / 120000000))	


void initClocks(void);
void initUSART(void);
void initGPIO(void);

int32_t SERCOM0_read(char *const buf, const uint32_t length);
int32_t SERCOM0_write(const char *const buf, const uint32_t length);
int32_t SERCOM0_IsDataAvailable(void);

enum gpio_port { GPIO_PORTA, GPIO_PORTB, GPIO_PORTC, GPIO_PORTD, GPIO_PORTE };

int main(void)
{

	char * buffer = "hello";
	
	initClocks();
	initUSART();
	initGPIO();
	print_init();
	
	SERCOM0_write(buffer,5);

	//should be able to print now
	printf("this works\n");
	
    /* Replace with your application code */
    while (1) 
    {
		//printf("this works\n");
    }
}

void initGPIO(void)
{
	// set UART RX input
	PORT->Group[GPIO_PORTA].WRCONFIG.reg =
	PORT_WRCONFIG_PMUX(MUX_PA09C_SERCOM0_PAD1)
	| PORT_WRCONFIG_WRPMUX
	| PORT_WRCONFIG_PMUXEN
	| PORT_WRCONFIG_WRPINCFG
	| ((1 << 9) & 0xffff);

	// set UART TX output
	PORT->Group[GPIO_PORTA].WRCONFIG.reg =
	PORT_WRCONFIG_PMUX(MUX_PA08C_SERCOM0_PAD0)
	| PORT_WRCONFIG_WRPMUX
	| PORT_WRCONFIG_PMUXEN
	| PORT_WRCONFIG_WRPINCFG
	| ((1 << 8) & 0xffff);

}

void initUSART(void)
{
	
	//unmask SERCOM0 in MCLK to enable clock to user interface
	MCLK->APBAMASK.bit.SERCOM0_ = 1;
	
	//connect GCLK3 with SERCOM0_SLOW running at 32khz
	//connect GLCK0 with SERCOM0_CORE running at 120MHz
	GCLK->PCHCTRL[SERCOM0_GCLK_ID_CORE].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
	GCLK->PCHCTRL[SERCOM0_GCLK_ID_SLOW].reg = GCLK_PCHCTRL_GEN_GCLK3 | GCLK_PCHCTRL_CHEN;
	
	//do a software reset of the module (write-synchronized)
	SERCOM0->USART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
	while(SERCOM0->USART.SYNCBUSY.bit.SWRST);
	
	//configure USART mode and bit order
	SERCOM0->USART.CTRLA.reg = 
			SERCOM_USART_CTRLA_MODE(1)
		|	SERCOM_USART_CTRLA_RXPO(1)
		|	SERCOM_USART_CTRLA_TXPO(0)
		|	SERCOM_USART_CTRLA_DORD;
		
	//set character size to 8
	SERCOM0->USART.CTRLB.bit.CHSIZE = 0;
	
	//setup baud rate
	SERCOM0->USART.BAUD.reg = CONF_SERCOM_0_USART_BAUD_RATE_REGISTER_VAL;
	
		
	//enable transmitter and receiver
	SERCOM0->USART.CTRLB.reg = 
			SERCOM_USART_CTRLB_TXEN
		|	SERCOM_USART_CTRLB_RXEN;
	while(SERCOM0->USART.SYNCBUSY.bit.CTRLB);
	

	
	//enable USART
	SERCOM0->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
	while(SERCOM0->USART.SYNCBUSY.bit.ENABLE);

}


int32_t SERCOM0_write(const char *const buf, const uint32_t length) {
	uint32_t offset = 0;
	
	while(!(SERCOM0->USART.INTFLAG.bit.DRE));
	
	do {
		SERCOM0->USART.DATA.reg = buf[offset];
		while(!(SERCOM0->USART.INTFLAG.bit.DRE));
	} while (++offset < length);
	
	while(!(SERCOM0->USART.INTFLAG.bit.TXC));
	
	return (int32_t)offset;
}

int32_t SERCOM0_read(char *const buf, const uint32_t length) {
	uint32_t offset = 0;
	
	do {
		while(!(SERCOM0->USART.INTFLAG.bit.RXC));
		buf[offset] = SERCOM0->USART.DATA.reg;
	} while (++offset < length);

	return (int32_t)offset;
}

int32_t SERCOM0_IsDataAvailable(void)
{
	return (SERCOM0->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_RXC) ? 1 : 0;
}

void initClocks(void)
{
	/*-------CLOCK CONFIGURATION----------------------------------------------/
	
	GCLK0 source:	DPLL0	(120 MHz)
	GCLK0 sink:		MCLK (CPU)
	
	GCLK2 source:	DFLL48M	(48 MHZ)
	GCLK2 sink:		Peripheral core
	
	GCLK3 source:	XOSC32k	(32.768kHz)
	GCLK3 sink:		Peripheral slow
	
	XOSC32k source:	Clock input on PA01 & PA02
	XOSC32k sink:	DPLL0
	XOSC32k sink:	GCLK3
	
	DPLL0 source:	XOSC32k (32.768kHz)
	DPLL0 sink:		GCLK0 (120MHz)
	
	DFLL48M source:	GCLK3
	DFLL48M sink:	GCLK2
	
	Steps to set up clocks in order:
	
	1) Setup XOSC32k
	2) Setup GCLK3 for 32kHz
	3) Setup DPLL0 for 120MHz
	4) Setup GCLK0 for 120MHz
	5) Setup DFLL48M for 48MHz
	6) Setup GCLK2 for 48MHz
	7) Setup Core and slow clocks for Peripherals
	8) Setup MCLK, including CLK_CPU and CLK_APBx, CLK_AHBx.

	*/
	//Set wait states
	NVMCTRL->CTRLA.bit.RWS = 5;		//5ws for 120MHz
	
	//Gclk reset
	GCLK->CTRLA.bit.SWRST;

	//OSCILLATOR CONTROL
	//STEP 1) Setup XOSC32K
	OSC32KCTRL->XOSC32K.bit.CGM = 01;
	OSC32KCTRL->XOSC32K.bit.XTALEN = 1;
	OSC32KCTRL->XOSC32K.bit.EN32K = 1;
	OSC32KCTRL->XOSC32K.bit.ONDEMAND = 1;
	OSC32KCTRL->XOSC32K.bit.RUNSTDBY = 1;
	OSC32KCTRL->XOSC32K.bit.STARTUP = 0;
	OSC32KCTRL->XOSC32K.bit.ENABLE = 1;	
	OSC32KCTRL->CFDCTRL.bit.CFDPRESC = 0;
	OSC32KCTRL->CFDCTRL.bit.SWBACK = 0;
	OSC32KCTRL->CFDCTRL.bit.CFDEN = 0;
	OSC32KCTRL->EVCTRL.bit.CFDEO = 0;
	
	// make sure osc32kcrtl is ready
	while (!OSC32KCTRL->INTFLAG.bit.XOSC32KRDY);
	
	//GCLK3 Control
	//Step 2) Setup GCLK3 for 32kHz
	GCLK->GENCTRL[3].reg = 
			GCLK_GENCTRL_GENEN
		|	GCLK_GENCTRL_SRC_XOSC32K;
		
	//DPLL0 SETUP
	//Step 3) Setup DPLL0 for 120 MHz 
	OSCCTRL->Dpll[0].DPLLCTRLB.bit.DIV = 1;
	OSCCTRL->Dpll[0].DPLLCTRLB.bit.DCOEN = 0;
	OSCCTRL->Dpll[0].DPLLCTRLB.bit.LBYPASS = 1;
	OSCCTRL->Dpll[0].DPLLCTRLB.bit.LTIME = 0;
	OSCCTRL->Dpll[0].DPLLCTRLB.bit.REFCLK = 1;	//Sets input to XOSC32k
	OSCCTRL->Dpll[0].DPLLCTRLB.bit.WUF = 1;
	OSCCTRL->Dpll[0].DPLLRATIO.reg = (3<<16) + 0xe4d;
		
	OSCCTRL->Dpll[0].DPLLCTRLA.bit.ONDEMAND = 0;
	OSCCTRL->Dpll[0].DPLLCTRLA.bit.RUNSTDBY = 1;
	OSCCTRL->Dpll[0].DPLLCTRLA.bit.ENABLE = 1;

	//Per errata 2.13.1
	while(!(OSCCTRL->Dpll[0].DPLLSTATUS.bit.CLKRDY == 1))
		;
		
	//GCLK0 Control
	//3) Setup DPLL0 for 120MHz
	GCLK->GENCTRL[0].reg = 
			GCLK_GENCTRL_GENEN
		|	GCLK_GENCTRL_SRC_DPLL0;
		
		
	//Setup DFLL48M
	//5) Setup DFLL48M for 48MHz
	OSCCTRL->DFLLCTRLB.bit.MODE = 1;
	OSCCTRL->DFLLCTRLB.bit.WAITLOCK = 1;
	OSCCTRL->DFLLCTRLA.bit.ENABLE = 1;
	OSCCTRL->DFLLCTRLA.bit.ONDEMAND = 0;
	OSCCTRL->DFLLCTRLA.bit.RUNSTDBY = 1;
	
	
	//GCLK2 Control
	//Setup GCLK2 for 48MHz
	GCLK->GENCTRL[2].reg =
			GCLK_GENCTRL_GENEN
		|	GCLK_GENCTRL_SRC_DFLL;
	
	
	GCLK->GENCTRL[0].reg = (0 << 16) | (0x21 << 8) | 7; // dpll0 = 7, enabled and standby = 0x21, divide by 1
	while (GCLK->SYNCBUSY.bit.GENCTRL0)
		;
		
	GCLK->GENCTRL[1].reg = (100 << 16) | (0x21 << 8) | 7; // dpll0 = 7, enabled and standby = 0x21, divide by 100
	while (GCLK->SYNCBUSY.bit.GENCTRL1)
		;
		
	MCLK->CPUDIV.reg = MCLK_CPUDIV_DIV_DIV1;
	
	
}
