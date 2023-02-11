// Ambiclone Mono project
// Try to emulate a Ambilight system by placing single board modules around a screen.
//
// Modules are based around a STC8G1K08A MCU, a VEML6040 color sensor and a SK6812 LED.
//
// This project meant to be compiled in VScode IDE with PlatformIO.
// It relies on FwLib_STC8 by IOsetting (https://github.com/IOsetting/FwLib_STC8).
// Specific notes about FwLib_STC8:
//     If you plan to develop using it, right after starting VScode, switch PlatformIO project environment to "env:STC8G1K08A" first then compile.
//     After this, switch to "env:env_editor" to avoid Intellisense syntax errors linked to SDCC syntax (no need to switch back to "env:STC8G1K08A" to compile then (until IDE restart)).
// 
// FwLib_STC8 files in this repository will differs from current author's git.
// Parts of the library are commented to avoid unused functions/vars inclusion overhead on flash/sram (linked to some limitations of SDCC).
// 
// Firmware is compiled to ./.pio/build/STC8G1K08A/firmware.hex
//
// /!\ Few warnings:
// - All files of this repository are provided as-is.
// - Build or use (code/module) at your own risk.
// - Most of provided codes aren't optimized for 8051 MCU.
//
// Serial commands (assuming UART port not disable by DISABLE_UART1):
// /!\ 31 bytes per commands max (incl \r\n), no overflow checks, unsigned numbers only, assuming a value of 0 if no value provided.
//     Sensor related (0 to 65535):
//          'r': Red channel amplification.
//          'g': Green channel amplification.
//          'b': Blue channel amplification.
//          Amplification represent the value that will be used to expand sensor channels value.
//          (corrected channel value) = ((channel value) * 65535) / (amplification value)
//          'o': Output current sensor channels values (raw and corrected) to UART, >0 to enable else disabled.
//     LED related (0-1):
//          't': Led test mode (RGB cycle with given value every LED_TEST_CYCLE_DURATION milliseconds, not saved to eeprom).
//     EEPROM related (0-1):
//          's1': Save sensor channels amplification values to eeprom.
//          'e1': Export eeprom sector 0 content to UART1 with Intel HEX format, recommended when flashing multiple modules.
//
//     Command examples:
//         "r12b10s1" set red to 12, blue to 10, save to eeprom.
//         "rgb" reset red, green, blue amplifications to 0.

#include "fw_hal.h"
#include "FwLib_STC8_helper.h"

//User settings
    //UART1
    #define UART1_BAUDRATE 115200UL //serial baudrate.
    #define UART1_STRING_TERMINATION 1U //TX only, 0 for '\n' else "\r\n".
    #define UART1_ENABLE_U32_SUPPORT 0U //enable unsigned 32bits numbers.

    //I2C
    #define I2C_FREQ 100000UL //bus frequency in hz.

    //VEML6040 sensor settings
    #define VEML6040_ADDR 0x20 //i2c address with write bit (7bits address converted to 8).
    #define VEML6040_CONF_IT 0x50 //integration time setting. 0x00:40ms, 0x10:80ms, 0x20:160ms, 0x30:320ms, 0x40:640ms, 0x50:1280ms.
    #define VEML6040_CONF_TRIG 0x00 //proceed one detecting cycle at manual force mode. 0x00:not trigger, 0x04:trigger one time detect cycle.
    #define VEML6040_CONF_AF 0x00 //auto/manual force mode. 0x00:auto, 0x02:force.
    #define VEML6040_CONF_SD 0x00 //chip shutdown setting (color sensor). 0x00:enable, 0x01:disable.

    //VEML6040 channels color amplification, 0-65535(0xFFFF).
    //only used at first start, then value will be loaded from EEPROM if EEPROM functions not disabled.
    //White channel is never used.
    #define DEFAULT_SENSOR_AMPLIFICATION_RED    0xFFFF
    #define DEFAULT_SENSOR_AMPLIFICATION_GREEN  0xFFFF
    #define DEFAULT_SENSOR_AMPLIFICATION_BLUE   0xFFFF

    //SK6812 LED
    #define LED_UPDATE_RATE 25U //led update rate in hz, better if matching VEML6040_CONF_IT (e.g 0x00=40ms mean 25 updates a sec), MCU_ENABLE_POWERDOWN_MODE need to be set to 1.
    #define LED_TEST_CYCLE_DURATION 500U //interval between each color cycles during test mode, in msec.

    //EEPROM
    #define EEPROM_ADDR 0U //eeprom start address.
    #define EEPROM_MAGIC_SIZE 5U //magic array size.
    __PDATA uint8_t eepromMagic[EEPROM_MAGIC_SIZE] = {'N','S','A','C','L'}; //magic used to check eeprom validity.

    //Power down mode
    #define MCU_ENABLE_POWERDOWN_MODE 0 ///!\ WARNING, incompatible with UART RX, enable power down mode after led colors updated if 1.

    //Debug
    #define DEBUG_OUTPUT 1  //1 to enable serial debug output.
    #define DISABLE_UART1 0 //1 to disable serial thru UART1.
    #define DISABLE_WATCHDOG 1 ///!\ WARNING, NOT TESTED. 1 to disable hardware watchdog.
    #define DISABLE_EEPROM 0 //1 to disable all EEPROM functions.
    #define DISABLE_EEPROM_READ 0 //1 to avoid vars from EEPROM reading when MCU boots.
    #define DISABLE_SENSOR 0 //1 to disable VEML6040 sensor and I2C, set LED to color cycle mode.
    #define FORCE_SENSOR_OUTPUT 0 //1 to force sensor output to UART1.
    #define DISABLE_LED 0 //1 to disable SK6812 LED.


//Failsafes
#if !(defined(__CONF_FOSC) || __CONF_FOSC == 11059200UL)
    #error "SK6812 LED timing require MCU frequency to be set to 11059200UL"
#endif
#ifndef __CONF_CLKDIV
    #define __CONF_CLKDIV 1U /*set a clock divider to 1 if not set*/
#endif
#if MCU_ENABLE_POWERDOWN_MODE
    #if LED_UPDATE_RATE > 1000U
        #error "LED_UPDATE_RATE can't exceed 1000U"
    #endif
    #if LED_TEST_CYCLE_DURATION > 16383500UL
        #error "LED_TEST_CYCLE_DURATION can't exceed 16383500UL"
    #endif
#endif


//Power down mode
#if !(DISABLE_SENSOR && DISABLE_LED) && MCU_ENABLE_POWERDOWN_MODE
    #define WAKEUP_COUNT_LED_TEST_CYCLE WAKEUP_TIMER_COUNT_COMPUTE(LED_TEST_CYCLE_DURATION) /*count for led test mode wait duration*/
    #define WAKEUP_COUNT_LED_UPDATE WAKEUP_TIMER_COUNT_COMPUTE(1000UL / LED_UPDATE_RATE) /*count to limit led update rate*/
    #define TIMER0_COUNTING_REG_VALUE TIMER01_COUNTING_COMPUTE(500UL) /*timer counting to 500usec to match wake up counter precision*/
    __DATA uint16_t timer0_counter = 0; //count 500usec beat to predict proper power down duration

    INTERRUPT(Timer0_Routine, EXTI_VectTimer0); //timer0 interrupt
#else
    #undef MCU_ENABLE_POWERDOWN_MODE
#endif


//Watchdog
#if !DISABLE_WATCHDOG
    #define WATCHDOG_PRESCALER WATCHDOG_PRESCALER_COMPUTE(LED_TEST_CYCLE_DURATION + 500UL) /*watchdog prescaler include led test cycle duration plus 500ms as a extra safety*/
#endif


//I2C
#if !DISABLE_SENSOR
    //compute nearest i2c prescaler
    #define TMP_I2C_PRESCALER I2C_PRESCALER_COMPUTE(I2C_FREQ)
    #define TMP_I2C_FREQUENCY0 I2C_FREQUENCY_COMPUTE(TMP_I2C_PRESCALER - 1U)
    #define TMP_I2C_FREQUENCY1 I2C_FREQUENCY_COMPUTE(TMP_I2C_PRESCALER)
    #define TMP_I2C_DIFF0 NUM_DIFF_ABS(I2C_FREQ, TMP_I2C_FREQUENCY0)
    #define TMP_I2C_DIFF1 NUM_DIFF_ABS(I2C_FREQ, TMP_I2C_FREQUENCY1)
    #if NUM_LOWER_BOOL(TMP_I2C_DIFF0, TMP_I2C_DIFF1)
        #define I2C_PRESCALER TMP_I2C_PRESCALER - 1U
    #else
        #define TMP_I2C_FREQUENCY2 I2C_FREQUENCY_COMPUTE(TMP_I2C_PRESCALER + 1U)
        #define TMP_I2C_DIFF2 NUM_DIFF_ABS(I2C_FREQ, TMP_I2C_FREQUENCY2)
        #if NUM_LOWER_BOOL(TMP_I2C_DIFF2, TMP_I2C_DIFF1)
            #define I2C_PRESCALER TMP_I2C_PRESCALER + 1U
        #else
            #define I2C_PRESCALER TMP_I2C_PRESCALER
        #endif
    #endif

    void I2C_WaitInterruptFlag(void); //wait interrupt flag to be triggered
    void I2C_WriteU16(uint8_t /*addr*/, uint8_t /*reg*/, uint16_t /*regValue*/); //write word to device register
    uint16_t I2C_ReadU16(uint8_t /*addr*/, uint8_t /*reg*/); //read word from device register
#endif


//SK6812 LED
#if !(DISABLE_LED && DISABLE_SENSOR && DISABLE_UART1)
    __PDATA uint8_t ledTestColor = INLINE_IF_VAL_MACRO(DISABLE_SENSOR, 255U, 0); //test color
#endif


//VEML6040 sensor
#if !(DISABLE_SENSOR && DISABLE_EEPROM && DISABLE_UART1)
    __PDATA uint16_t sensorAmplificationArr[3] = { //channel amplification array, loaded from eeprom
            DEFAULT_SENSOR_AMPLIFICATION_RED, //red
            DEFAULT_SENSOR_AMPLIFICATION_GREEN, //green
            DEFAULT_SENSOR_AMPLIFICATION_BLUE, //blue
    };

    __CODE uint8_t sensorColorName[3]={'R','G','B'};

    #if !DISABLE_UART1
        #if (DISABLE_LED || FORCE_SENSOR_OUTPUT)
            __PDATA uint8_t sensorOutputUart1 = 1;
        #else
            __PDATA uint8_t sensorOutputUart1 = 0;
        #endif
    #endif
#endif


//EEPROM
#if !DISABLE_EEPROM
    //order: eepromFlashCount(4), magic(5), sensorAmplificationArr[0](2), sensorAmplificationArr[1](2), sensorAmplificationArr[2](2)
    __PDATA uint32_t eepromFlashCount; //store eeprom bank flash count, wrtie after magic. loaded from eeprom

    void EEPROM_readU8(uint16_t /*addr*/); //read one byte from eeprom
    void EEPROM_writeU8(uint16_t /*addr*/, uint8_t /*val*/); //write one byte from eeprom
    void EEPROM_vars(__BIT /*readMode*/, __BIT /*compare*/); //combine eeprom read/write to limit flash use.
    #if !DISABLE_UART1
        void EEPROM_exportIntelHex(uint8_t /*eepromSectors*/, uint16_t /*sectorSize*/); //export whole eeprom content to UART1 with Intel HEX format
    #endif
#endif


//UART1
#if !DISABLE_UART1
    #define UART1_TIMER_COUNTING_REG_VALUE UART_TIMER_COUNTING_COMPUTE(UART1_BAUDRATE) /*timer counting to match wanted baudrate*/

    #define UART1_RX_BUFFER_SIZE 32U //rx buffer size
    __PDATA uint8_t UART1RxBuffer[UART1_RX_BUFFER_SIZE]; //rx buffer
    __DATA uint8_t UART1RxBufferPos = 0; //rx buffer current chr position
    __BIT UART1StateBusy = 0; //rx line is receiving data
    __BIT UART1StateReceived = 0; //eol received

    #if UART1_STRING_TERMINATION //uart tx string termination
        #define UART1_STRING_END {UART1_TxChar('\r'); UART1_TxChar('\n');}
    #else
        #define UART1_STRING_END {UART1_TxChar('\n');}
    #endif

    #define UART1_ELEMENTS_PTRARR_SIZE 5U //updatable elements count
    __PDATA void* elementsPtrArr[UART1_ELEMENTS_PTRARR_SIZE] = { //elements pointer array
        sensorAmplificationArr, sensorAmplificationArr+1, sensorAmplificationArr+2, //red, green, blue
        &ledTestColor, //led test mode
        &sensorOutputUart1, //sensor output to uart1
    };

    __PDATA uint8_t elementsPtrArrVarSize[UART1_ELEMENTS_PTRARR_SIZE] = { //elements pointer array vars size in byte. 1:uint8_t, 2:uint16_t, 4:uint32_t
        2, 2, 2, //red, green, blue
        1, 1, //led test mode, sensor output to uart1
    };

    //UART elements position in elementsPtrArr, also used for updated elements parsing
    #define UART1_RX_ELEMENT_R 0U //red
    #define UART1_RX_ELEMENT_G 1U //green
    #define UART1_RX_ELEMENT_B 2U //blue
    #define UART1_RX_ELEMENT_T 3U //led test mode
    #define UART1_RX_ELEMENT_O 4U //sensor output to uart1
    #define UART1_RX_ELEMENT_S 5U //save eeprom
    #define UART1_RX_ELEMENT_E 6U //export eeprom

    INTERRUPT(UART1_Routine, EXTI_VectUART1); //uart1 rx interrupt
    uint8_t UART1_parseBuffer(void); //extract data from buffer
    void UART1_TxU32(uint32_t /*num*/); //print U32 number to uart1 to ascii number

    #define UART1_PRINT_VAR_VAL(__VAR_STR__, __VAL_STR__) {UART1_TxString(__VAR_STR__); UART1_TxChar(':'); UART1_TxString(__VAL_STR__); UART1_STRING_END}
    #define UART1_PRINT_VAR_VAL_U32(__VAR_STR__, __VAL__) {UART1_TxString(__VAR_STR__); UART1_TxChar(':'); UART1_TxU32(__VAL__); UART1_STRING_END}
    #define UART1_PRINT_STR(__STR__) {UART1_TxString(__STR__); UART1_STRING_END}

    #if DEBUG_OUTPUT
        #define DEBUG_UART1_PRINT_VAR_VAL(__VAR_STR__, __VAL_STR__) {UART1_TxString(__func__); UART1_TxChar(':'); UART1_PRINT_VAR_VAL(__VAR_STR__, __VAL_STR__);}
        #define DEBUG_UART1_PRINT_VAR_VAL_U32(__VAR_STR__, __VAL__) {UART1_TxString(__func__); UART1_TxChar(':'); UART1_PRINT_VAR_VAL_U32(__VAR_STR__, __VAL__);}
        #define DEBUG_UART1_PRINT_STR(__STR__) {UART1_TxString(__func__); UART1_TxChar(':'); UART1_PRINT_STR(__STR__);}
    #endif
#else //fake funts/macros to avoid compiler errors
    #undef DEBUG_OUTPUT
    #define DEBUG_OUTPUT 0
    void UART1_TxU32(uint32_t num){(void)num;}
    #define UART1_PRINT_VAR_VAL(__VAR_STR__, __VAL_STR__)
    #define UART1_PRINT_VAR_VAL_U32(__VAR_STR__, __VAL__)
    #define UART1_PRINT_STR(__STR__)
    #define UART1_STRING_END
#endif

#if !DEBUG_OUTPUT
    #define DEBUG_UART1_PRINT_VAR_VAL(__VAR_STR__, __VAL_STR__)
    #define DEBUG_UART1_PRINT_VAR_VAL_U32(__VAR_STR__, __VAL__)
    #define DEBUG_UART1_PRINT_STR(__STR__)
#endif


//Generic
void main(void);


