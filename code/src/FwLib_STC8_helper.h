// This file is here to assist FwLib_STC8 library
// It mainly provides preprocessor macros to help reverse compute prescaler and other stuff
// NNS@2023
// 
// /!\ Warning:
// - Code provided as is.
// - All numbers are assumed as unsigned.
// - __CONF_FOSC and __CONF_CLKDIV needs to be defined in compiler command line.
// 
// Macros:
//     MCU:
//         __SYSCLOCK : precompute system clock based on __CONF_FOSC and __CONF_CLKDIV, doesn't allow MCU system clock update once compiled.
// 
//     Generic:
//         INLINE_IF_VAL_MACRO(__VAR__, __TRUE__, __FALSE__) : Inline IF. If __VAR__ true, pushes __TRUE__ else __FALSE__. Useful to set array size.
// 
//     Math:
//         NUM_DIFF_ABS(__NUM1__, __NUM2__) : Difference between given numbers, result always positive.
//         NUM_LOWER_BOOL(__NUM1__, __NUM2__) : Check if __NUM1__ < __NUM2__, if so return 1U else 0.
//         NUM_LIMIT(__NUM__, __MAX__) : Limit __NUM__ to __MAX__ value.
//         NUM_POWER2(__NUM__) : Power 2 of given number.
//         NUM_2POWERN(__NUM__) : 2 power __NUM__.
//         LOG2(__NUM__) : Log base 2 of __NUM__, based on https://stackoverflow.com/a/6835421 .
// 
//     I2C:
//         //Please see main.h I2C section to compute prescaler value to get nearest frequency wanted. Not the best way but a working way.
//         I2C_PRESCALER_COMPUTE(__FREQ__) : Compute I2C prescaler from given frequency, WARNING: because if integer rounding, prescaler may be off but 1 (mostly minus).
//         I2C_FREQUENCY_COMPUTE(__PRESCALER__) : Compute I2C frequency from given prescaler value.
// 
//     Timer:
//         TIMER01_COUNTING_COMPUTE(__USEC__) : Timer0/1 counting value using useconds value with 1T clock.
//     
//     UART:
//         UART_TIMER_COUNTING_COMPUTE(__BAUDRATE__) : UART Timer counting value using baudrate value with 1T clock.
// 
//     Power down timer, WKTC (wake up timer counter):
//         WAKEUP_32KFREQ : Internal 32KHz low speed IRC frequency, in Hz (fixed here, check idata address 0xf8,0xf9 to get factory value).
//         WAKEUP_TIMER_MSEC_COMPUTE(__COUNT__) : WKTC to milliseconds.
//         WAKEUP_TIMER_COUNT_COMPUTE(__MSEC__) : Milliseconds to WKTC.
// 
//     Watchdog
//         WATCHDOG_PRESCALER_COMPUTE(__MSEC__) : Milliseconds to prescaler, prescaler will be rounded up.
//         WATCHDOG_MSEC_COMPUTE(__PRESCALER__) : Prescaler to milliseconds.
// 


#ifndef __CONF_FOSC
    #error "__CONF_FOSC required."
#endif

#ifndef __CONF_CLKDIV
    #error "__CONF_CLKDIV required."
#endif

//Generic
#define INLINE_IF_VAL_MACRO(__VAR__, __TRUE__, __FALSE__) ((__VAR__ + 0U) ? (__TRUE__) : (__FALSE__)) /*inline if, return true or false*/

//Math
#define NUM_DIFF_ABS(__NUM1__, __NUM2__) ((__NUM1__ < __NUM2__) ? (__NUM2__ - __NUM1__) : (__NUM1__ - __NUM2__)) /*difference between given numbers, result always positive*/
#define NUM_LOWER_BOOL(__NUM1__, __NUM2__) ((__NUM1__ < __NUM2__) ? (1U) : (0)) /*check if __NUM1__ < __NUM2__, if so return 1U else 0.*/
#define NUM_LIMIT(__NUM__, __MAX__) ((__NUM__ > __MAX__) ? (__MAX__) : (__NUM__)) /*limit number to max value*/
#define NUM_POWER2(__NUM__) (__NUM__ * __NUM__) /*basic number*number.*/
#define NUM_2POWERN(__NUM__) (1UL << (__NUM__)) /*2^number*/
//LOG2, NBITS2, NBITS4, NBITS8, NBITS16, NBITS32 based on https://stackoverflow.com/a/6835421
#define NBITS2(__NUM__) ((__NUM__ & 2) ? (1U) : (0))
#define NBITS4(__NUM__) ((__NUM__ & 0xC) ? (2 + NBITS2(__NUM__ >> 2U)) : (NBITS2(__NUM__)))
#define NBITS8(__NUM__) ((__NUM__ & 0xF0) ? (4 + NBITS4(__NUM__ >> 4U)) : (NBITS4(__NUM__)))
#define NBITS16(__NUM__) ((__NUM__ & 0xFF00) ? (8 + NBITS8(__NUM__ >> 8U)) : (NBITS8(__NUM__)))
#define NBITS32(__NUM__) ((__NUM__ & 0xFFFF0000) ? (16 + NBITS16(__NUM__ >> 16U)) : (NBITS16(__NUM__)))
#define LOG2(__NUM__) ((__NUM__ == 0) ? (0) : (NBITS32(__NUM__) + 1U)) /*Log base 2 of a number*/

//MCU system clock
#ifndef __SYSCLOCK
    #define __SYSCLOCK (__CONF_FOSC / ((__CONF_CLKDIV == 0) ? 1 : __CONF_CLKDIV)) /*constant version of SYS_GetSysClock()*/
#endif

//I2C
//see main.h I2C section to compute prescaler value to get nearest frequency wanted
#define I2C_PRESCALER_COMPUTE(__FREQ__) ((((__SYSCLOCK / __FREQ__) / 2U) - 4U) / 2U) /*compute i2c prescaler given frequency*/
#define I2C_FREQUENCY_COMPUTE(__PRESCALER__) ((__SYSCLOCK / 2U) / (((__PRESCALER__) * 2U) + 4U)) /*compute i2c frequency given prescaler*/

//Timer
#define TIMER01_COUNTING_COMPUTE(__USEC__) (65536UL - (__SYSCLOCK / (1000000UL / __USEC__))) /*timer0/1 counting value using usec value with 1T clock*/

//UART
#define UART_TIMER_COUNTING_COMPUTE(__BAUDRATE__) (65536UL - (__SYSCLOCK / __BAUDRATE__ / 4U)) /*uart timer counting value using baudrate value with 1T clock*/

//Power down timer
#define WAKEUP_32KFREQ 32768UL /*UL to avoid overflow, idata 0xf9, 0xf8*/
#define WAKEUP_TIMER_COUNT_COMPUTE(__MSEC__) (((__MSEC__ * WAKEUP_32KFREQ) / (1000U * 16U)) - 1U) /*count from msec*/
#define WAKEUP_TIMER_MSEC_COMPUTE(__COUNT__) ((1000UL * 16U * (1U + __COUNT__)) / WAKEUP_32KFREQ) /*msec from count*/

//Watchdog
#define WATCHDOG_PRESCALER_COMPUTE(__MSEC__) (NUM_LIMIT((LOG2((((__SYSCLOCK / 32768UL) / 12U) * __MSEC__) / 1000UL) - 1U), 7U)) /*msec to prescaler*/
#define WATCHDOG_MSEC_COMPUTE(__PRESCALER__) ((32768UL * 12U * NUM_2POWERN(__PRESCALER__ + 1U)) / (__SYSCLOCK / 1000UL)) /*prescaler to msec*/


