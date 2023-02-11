// Ambiclone Mono project
// Try to emulate a Ambilight system by placing single board modules around a screen.
//
// User settings, serial commands, license and other things in main.h

#include "main.h"

//UART1 functions
#if !DISABLE_UART1
    INTERRUPT(UART1_Routine, EXTI_VectUART1){ //uart1: rx interrupt
        if (RI){
            UART1StateBusy = 1; //receiving
            UART1_ClearRxInterrupt();
            if (!UART1StateReceived){
                uint8_t chr = SBUF;
                if (chr == '\r' || chr == '\n' || UART1RxBufferPos > UART1_RX_BUFFER_SIZE - 2U){ //command end or buffer limit
                    UART1StateReceived = 1; //received
                    chr = '\0';
                    DEBUG_UART1_PRINT_STR("recv"); //debug
                }
                UART1RxBuffer[UART1RxBufferPos++] = chr;
            }
        }
    }

    uint8_t UART1_parseBuffer(void){ //uart1: extract data from buffer
        UART1_SetRxState(HAL_State_OFF); //disable rx line
        uint8_t elementUpdated = 0; //updated elements return
        uint8_t element = 0xFF, elementCurr = 0xFF, chrBack = ' ';
        #if UART1_ENABLE_U32_SUPPORT
            uint32_t number = 0;
        #else
            uint16_t number = 0;
        #endif
        #if DEBUG_OUTPUT
            UART1_TxString("buffer:\""); UART1_TxString(UART1RxBuffer); UART1_TxChar('"'); UART1_STRING_END
        #endif

        for (uint8_t i=0; i<UART1_RX_BUFFER_SIZE; i++){ //char loop
            uint8_t chr = UART1RxBuffer[i];
            if (elementCurr != 0xFF && chr < ('9' + 1U) && chr > ('0' - 1U)){number = (number * 10U) + (chr - '0'); //char to num conversion
            } else {
                if (chr == 'r'){element = UART1_RX_ELEMENT_R; //red
                } else if (chr == 'g'){element = UART1_RX_ELEMENT_G; //green
                } else if (chr == 'b'){element = UART1_RX_ELEMENT_B; //blue
                } else if (chr == 't'){element = UART1_RX_ELEMENT_T; //led test mode
                } else if (chr == 'o'){element = UART1_RX_ELEMENT_O; //sensor values output
                } else if (chr == 's'){element = UART1_RX_ELEMENT_S; //save eeprom
                } else if (chr == 'e'){element = UART1_RX_ELEMENT_E;  //export eeprom
                } else {element = 0xFF;} //invalid char
            }

            if (elementCurr != element){ //element changed
                if (elementCurr != 0xFF){ //element updated
                    UART1_TxChar(chrBack); UART1_TxChar(':'); UART1_TxU32(number); UART1_STRING_END //print back read values
                    if (elementCurr < UART1_ELEMENTS_PTRARR_SIZE){ //update element ptr value if allowed
                        uint8_t tmpSize = elementsPtrArrVarSize[elementCurr];
                        if (tmpSize == 1){*(uint8_t*)elementsPtrArr[elementCurr] = number;
                        } else if (tmpSize == 2){*(uint16_t*)elementsPtrArr[elementCurr] = number;
                        }
                        #if UART1_ENABLE_U32_SUPPORT
                            else if (tmpSize == 4){*(uint32_t*)elementsPtrArr[elementCurr] = number;}
                        #endif
                    }
                    elementUpdated |= (1U << elementCurr); //update return var
                    number = 0; 
                }
                elementCurr = element;
                chrBack = chr;
            }
            if (chr == '\0'){break;}
        }

        UART1RxBufferPos = 0; UART1StateBusy = 0; UART1StateReceived = 0; //reset buffer position and uart state
        UART1_PRINT_STR(elementUpdated ? "ok" : "err"); //uart responce
        UART1_SetRxState(HAL_State_ON); //re-enable rx line
        return elementUpdated;
    }

    void UART1_TxU32(uint32_t num){ //print U32 number to uart1 to ascii number, to optimize
        if (num){
            __PDATA uint8_t buf[12]; buf[11]='\0';
            for (uint8_t i=10; i>0; i--){
                buf[i] = (num % 10U) + '0';
                num /= 10U;
                if (!num){
                    UART1_TxString(buf + i);
                    break;
                }
            }
        } else {UART1_TxChar('0');}
    }
#endif


//timer0 interrupt
#if MCU_ENABLE_POWERDOWN_MODE
    INTERRUPT(Timer0_Routine, EXTI_VectTimer0){timer0_counter++;} //timer0 counter to mesure main loop duration
#endif


//EEPROM functions
#if !DISABLE_EEPROM
    void EEPROM_readU8(uint16_t addr){IAP_CmdRead(addr);} //read one byte from eeprom
    void EEPROM_writeU8(uint16_t addr, uint8_t val){IAP_WriteData(val); IAP_CmdWrite(addr);} //write one byte to eeprom

    void EEPROM_vars(__BIT readMode, __BIT compare){ //combine eeprom read/write to limit flash use.
        //readMode=1 will read (0 will write) data from eeprom. If magic doesn't match, magic/write count/vars will be rewrote in eeprom.
        //When readMode=1, if compare=0 and a eeprom var mismatch, magic/write count/vars will be rewrote in eeprom.

        IAP_SetEnabled(HAL_State_ON);
        uint16_t eepromAddr = EEPROM_ADDR; //current write address

        EEPROM_vars_functStart:; //jump point if in read mode and magic mismatch (rewrite eeprom)

        //erase sector in write mode
        if (!readMode){
            IAP_CmdErase(EEPROM_ADDR);
            if (IAP_IsCmdFailed()){
                UART1_PRINT_STR("EEPROM erase failed");
                goto EEPROM_vars_failure;
            }
        }

        //magic
        for (uint8_t i=0; i<EEPROM_MAGIC_SIZE; i++){
            if (readMode){EEPROM_readU8(eepromAddr);} else {EEPROM_writeU8(eepromAddr, eepromMagic[i]);}
            if (IAP_IsCmdFailed()){goto EEPROM_vars_failure;} //request failed
            if (readMode && eepromMagic[i] != IAP_ReadData()){
                UART1_PRINT_STR("EEPROM magic mismatch");
                compare = 0; readMode = 0; eepromAddr = EEPROM_ADDR; //reset vars for write cycle
                goto EEPROM_vars_functStart; //rewrite eeprom
            }
            eepromAddr++;
        }

        //eeprom flash count
        if (!readMode){eepromFlashCount++;}
        __PDATA uint8_t* eepromFlashCountPtr = (uint8_t*)&eepromFlashCount;
        for (uint8_t i=0; i<4; i++){
            if (readMode){EEPROM_readU8(eepromAddr);} else {EEPROM_writeU8(eepromAddr, *(eepromFlashCountPtr + i));}
            if (IAP_IsCmdFailed()){goto EEPROM_vars_failure;} //request failed
            if (readMode){*(eepromFlashCountPtr + i) = IAP_ReadData();}
            eepromAddr++;
        }
        if (!readMode){UART1_PRINT_VAR_VAL_U32("EEPROM writes", eepromFlashCount);} //report write counts

        //vars
        for (uint8_t i=0; i<3; i++){
            for (uint8_t byte=0; byte<2; byte++){
                uint8_t* varPtr = ((uint8_t*)&sensorAmplificationArr[i]) + byte;
                if (readMode){EEPROM_readU8(eepromAddr);} else {EEPROM_writeU8(eepromAddr, *(varPtr));}
                if (IAP_IsCmdFailed()){goto EEPROM_vars_failure;} //request failed
                if (readMode){
                    if (*(varPtr) != IAP_ReadData()){
                        if (compare){
                            UART1_PRINT_STR("EEPROM var mismatch");
                            compare = 0; readMode = 0; eepromAddr = EEPROM_ADDR; //reset vars for write cycle
                            goto EEPROM_vars_functStart; //rewrite eeprom
                        } else {*(varPtr) = IAP_ReadData();} //eeprom -> var
                    }
                }
                eepromAddr++;
            }
        }

        IAP_ClearCmdFailFlag();
        IAP_SetEnabled(HAL_State_OFF);
        if (!readMode){UART1_PRINT_STR("EEPROM saved");}
        return;

    EEPROM_vars_failure:; //failure jump point
        IAP_ClearCmdFailFlag();
        IAP_SetEnabled(HAL_State_OFF);
        UART1_PRINT_STR("EEPROM failure");
    }

    #if !DISABLE_UART1
        void EEPROM_exportIntelHex(uint8_t eepromSectors, uint16_t sectorSize){ //export whole eeprom content to UART1 with Intel HEX format
            // Export EEPROM given amount of "eepromSectors" sectors, for STC8G each sectors are 512 bytes so 8 sectors max (STC8G1K08A), following datasheet: sector=eepromAdress>>9, sectorSize should be 512.
            // If "eepromSectors" equal 0 or > 128 or exceed sector count, function will stop/hang when fail happen.
            // Reference: https://en.wikipedia.org/wiki/Intel_HEX
            // HEX values are w/o "0x".
            // Line format:
            //   ':' : start char.
            //   'HH' : 8 bits HEX : bytes count, 16bytes here (0x10).
            //   'HHHH' : 16 bits HEX : memory address offset.
            //   'HH' : 8 bits HEX : record type, always 00 here to assume as data type.
            //   "DATA" : 16 x 8 bytes.
            //   'HH' : 8 bits HEX : sumcheck : ~((bytes count) + (address MSB) + (address LSB) + (record type) + **EACH**(dataByte)) + 1.

            //begin of report
            IAP_SetEnabled(HAL_State_ON);
            UART1_TxString("EEPROM export, "); 
            UART1_TxU32(eepromSectors); UART1_TxString(" sector(s) (");
            UART1_TxU32(sectorSize); UART1_PRINT_STR(" bytes)");
            UART1_PRINT_STR("##########"); UART1_STRING_END;

            uint16_t eepromAddr = 0;
            uint16_t eepromAddrEnd = sectorSize * eepromSectors - 1U;
            while (1){
                uint16_t sum = 0; uint8_t tmp = 0; //current line sum
                UART1_TxChar(':'); //start char
                UART1_TxHex(16U); sum += 16U; //byte count
                tmp = (uint8_t)(eepromAddr >> 8); UART1_TxHex(tmp); sum += tmp; //memory address msb
                tmp = (uint8_t)(eepromAddr & 0xff); UART1_TxHex(tmp); sum += tmp; //memory address lsb
                UART1_TxHex(0U); //record type: data
                for (uint8_t i=0; i<16; i++){ //data
                    EEPROM_readU8(eepromAddr++);
                    if (!IAP_IsCmdFailed()){UART1_TxHex(IAP_ReadData()); sum += IAP_ReadData(); //current byte
                    } else {goto EEPROM_exportIntelHex_failure;} //request failed, break
                }
                sum = (uint8_t)(~sum + 1); UART1_TxHex(sum); //sum
                UART1_STRING_END; //new line
                if (eepromAddr > eepromAddrEnd){break;} //safety
            }
            UART1_PRINT_STR(":00000001FF"); //eof

            //end of report
            UART1_STRING_END; UART1_PRINT_STR("##########");
            UART1_TxString("exported:"); UART1_TxU32(eepromAddr); UART1_TxString("bytes"); UART1_STRING_END;

            IAP_ClearCmdFailFlag();
            IAP_SetEnabled(HAL_State_OFF);
            return;

        EEPROM_exportIntelHex_failure: //failure jump point
            IAP_ClearCmdFailFlag();
            IAP_SetEnabled(HAL_State_OFF);
            UART1_STRING_END;
            UART1_PRINT_STR("EEPROM failure");
        }
    #endif
#endif


//I2C functions
#if !DISABLE_SENSOR
    void I2C_WaitInterruptFlag(void){ //wait interrupt flag to be triggered
        while (!(I2CMSST & 0x40));
        I2CMSST &= ~0x40;
    }

    void I2C_WriteU16(uint8_t addr, uint8_t reg, uint16_t regValue){ //write word to device register
        SFRX_ON();

        I2CTXD = addr & 0xFE; //slave addr + write bit
        I2CMSCR = 0x09; //1001: Start + send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CTXD = reg;
        I2CMSCR = 0x0A; //1010: Send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CTXD = regValue & 0xff;
        I2CMSCR = 0x0A; //1010: Send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CTXD = regValue >> 8;
        I2CMSCR = 0x0A; //1010: Send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CMSCR = 0x06; //0110: Stop
        I2C_WaitInterruptFlag();

        SFRX_OFF();
    }

    uint16_t I2C_ReadU16(uint8_t addr, uint8_t reg){ //read word from device register
        SFRX_ON();

        I2CTXD = addr & 0xFE; //slave addr + write bit
        I2CMSCR = 0x09; //1001: Start + send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CTXD = reg;
        I2CMSCR = 0x0A; //1010: Send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CTXD = addr | 0x01; //slave addr + read bit
        I2CMSCR = 0x09; //1001: Start + send data + receive ACK
        I2C_WaitInterruptFlag();

        I2CMSCR = 0x0B; //1011: Receive data + send ACK
        I2C_WaitInterruptFlag();
        uint16_t ret = I2CRXD;
        
        I2CMSCR = 0x0C; //1100: Receive data + send NAK
        I2C_WaitInterruptFlag();
        //ret = (uint16_t)(ret << 8) | I2CRXD;
        ret |= (uint16_t)I2CRXD << 8;

        I2CMSCR = 0x06; //0110: Stop
        I2C_WaitInterruptFlag();

        SFRX_OFF();
        return ret;
    }
#endif


//generic functions
void main(void){
    //UART1 on Timer1 init
    #if !DISABLE_UART1
        SM0 = 0; SM1 = 1; //8-bit UART, whose baud-rate is variable
        UART1_SetBaudSource(UART1_BaudSource_Timer1);
        TIM_Timer1_Set1TMode(HAL_State_ON);
        TIM_Timer1_SetMode(TIM_TimerMode_16BitAuto);//TIM_TimerMode_16BitAuto:0
        TIM_Timer1_SetInitValue(UART1_TIMER_COUNTING_REG_VALUE >> 8, UART1_TIMER_COUNTING_REG_VALUE);
        TIM_Timer1_SetRunState(HAL_State_ON);
        UART1_SetRxState(HAL_State_ON); //enable rx line
        EXTI_UART1_SetIntPriority(3U); //highest priority
        EXTI_UART1_SetIntState(HAL_State_ON); //enable uart1 interrupt
    #endif
    
    //Timer0 init, counter to evaluate loop duration for power down mode
    //500us interval to match Power Down Wakeup Timer Countdown resolution
    #if MCU_ENABLE_POWERDOWN_MODE
        TIM_Timer0_Set1TMode(HAL_State_ON);
        TIM_Timer0_SetMode(TIM_TimerMode_16BitAuto);
        TIM_Timer0_SetInitValue((uint16_t)(TIMER0_COUNTING_REG_VALUE) >> 8, (uint16_t)(TIMER0_COUNTING_REG_VALUE) & 0xFF);
        //EXTI_Timer0_SetIntPriority(__PRIORITY__);
        TIM_Timer0_SetRunState(HAL_State_ON); //start timer0
        EXTI_Timer0_SetIntState(HAL_State_ON); //enable timer0 interrupt
    #endif

    #if (MCU_ENABLE_POWERDOWN_MODE || !DISABLE_UART1)
        EXTI_Global_SetIntState(HAL_State_ON); //enable interrupts
    #endif

    //EEPROM init
    #if !DISABLE_EEPROM
        IAP_SetWaitTime();
        #if !DISABLE_EEPROM_READ
            EEPROM_vars(1U, 0); //read eeprom to vars
        #endif
    #endif

    //Power down mode init
    #if MCU_ENABLE_POWERDOWN_MODE
        //TODO: push fixed fw_rcc.h to github
        RCC_SetPowerDownWakeupTimerState(HAL_State_ON);
    #endif

    //VEML6040 init
    #if !DISABLE_SENSOR
        GPIO_P3_SetMode(GPIO_Pin_3, GPIO_Mode_InOut_QBD); //GPIO P3.3 (sda)
        GPIO_P3_SetMode(GPIO_Pin_2, GPIO_Mode_Output_PP); //GPIO P3.2 (scl)

        I2C_SetWorkMode(I2C_WorkMode_Master); //i2c master mode
        I2C_SetClockPrescaler(I2C_PRESCALER); //set prescaler
        DEBUG_UART1_PRINT_VAR_VAL_U32("I2C_PRESCALER", I2C_PRESCALER); //debug
        DEBUG_UART1_PRINT_VAR_VAL_U32("I2C_FREQ", I2C_FREQUENCY_COMPUTE(I2C_PRESCALER)); //debug
        I2C_SetEnabled(HAL_State_ON); //enable i2c
        I2C_SetMasterAutoSend(HAL_State_OFF);

        SYS_Delay(100);
        I2C_WriteU16(VEML6040_ADDR, 0x00, (uint16_t)(VEML6040_CONF_IT|VEML6040_CONF_TRIG|VEML6040_CONF_AF|VEML6040_CONF_SD) << 8); //initial sensor config
    #else
        DEBUG_UART1_PRINT_STR("sensor disabled");
    #endif

    //SK6812 init
    #if !DISABLE_LED
        GPIO_P5_SetMode(GPIO_Pin_5, GPIO_Mode_Output_PP); //GPIO P5.5 as output
        uint8_t ledTestColorCur = 0; //test color current color, 0:r, 1:g, 2:b
    #else
        DEBUG_UART1_PRINT_STR("led disabled");
    #endif

    //Watchdog init
    #if !DISABLE_WATCHDOG
        WDT_SetCounterPrescaler(WATCHDOG_PRESCALER);
        DEBUG_UART1_PRINT_VAR_VAL_U32("WATCHDOG_PRESCALER", WATCHDOG_PRESCALER); //debug
        DEBUG_UART1_PRINT_VAR_VAL_U32("WATCHDOG_MSEC", WATCHDOG_MSEC_COMPUTE(WATCHDOG_PRESCALER)); //debug
        WDT_EnableCounterWhenIdle(HAL_State_OFF);
        WDT_StartWatchDog();
    #endif

    //Main loop
    while(1){
        #if !DISABLE_UART1
            while(UART1StateBusy && !UART1StateReceived){;} //wait if uart receiving data

            if (UART1StateReceived){ //uart data received, parse buffer
                uint8_t elementUpdated = UART1_parseBuffer(); //extract data from buffer

                if (elementUpdated){ //something to update
                    //if (elementUpdated & ((uint8_t )(1U) << UART1_RX_ELEMENT_R)){ //red
                    //    DEBUG_UART1_PRINT_STR("red"); //debug
                    //}
                    //
                    //if (elementUpdated & ((uint8_t )(1U) << UART1_RX_ELEMENT_G)){ //green
                    //    DEBUG_UART1_PRINT_STR("green"); //debug
                    //}
                    //
                    //if (elementUpdated & ((uint8_t )(1U) << UART1_RX_ELEMENT_B)){ //blue
                    //    DEBUG_UART1_PRINT_STR("blue"); //debug
                    //}
                    //
                    //if (elementUpdated & ((uint8_t )(1U) << UART1_RX_ELEMENT_T)){ //test mode
                    //    DEBUG_UART1_PRINT_STR("test"); //debug
                    //}
                
                    if (elementUpdated & ((uint8_t )(1U) << UART1_RX_ELEMENT_S)){ //save to eeprom
                        DEBUG_UART1_PRINT_STR("save"); //debug
                        #if !DISABLE_EEPROM
                            EEPROM_vars(0, 1U); //compare eeprom to vars, rewrite eeprom if mismatch
                        #endif
                    }

                    if (elementUpdated & ((uint8_t )(1U) << UART1_RX_ELEMENT_E)){ //export eeprom to uart
                        DEBUG_UART1_PRINT_STR("exp"); //debug
                        #if !DISABLE_EEPROM
                            EEPROM_exportIntelHex(1U, 512UL); //export eeprom content to UART1 with Intel HEX format
                        #endif
                    }
                }
            }
        #endif

        #if !DISABLE_SENSOR //VEML6040 sensor
            uint16_t sensorColorArr[3]; //colors array
            if (!ledTestColor){ //not in led test mode
                #if !DISABLE_UART1
                    if (sensorOutputUart1){UART1_PRINT_STR("sensor:");}; //output to uart1
                #endif

                for (uint8_t i=0; i<3; i++){ //channels loop
                    uint16_t tmpColor = I2C_ReadU16(VEML6040_ADDR, 0x08 + i);

                    #if !DISABLE_UART1
                        if (sensorOutputUart1){UART1_TxChar(sensorColorName[i]); UART1_TxChar(':'); UART1_TxU32(tmpColor);} //output to uart1
                    #endif

                    if (sensorAmplificationArr[i] != 0xFFFF){ //apply correction
                        uint32_t tmpColorU32 = ((uint32_t)tmpColor * 0xFFFF) / sensorAmplificationArr[i];
                        if(tmpColorU32 > 0xFFFF){tmpColorU32 = 0xFFFF;} //u16 overflow
                        tmpColor = tmpColorU32;
                    }
                    tmpColor >>= 8U; //convert to 8bits

                    #if !DISABLE_UART1
                        if (sensorOutputUart1){UART1_TxChar(','); UART1_TxU32(tmpColor); UART1_STRING_END;} //output to uart1
                    #endif

                    sensorColorArr[i] = tmpColor;
                }

                #if !DISABLE_UART1
                    if (sensorOutputUart1){UART1_STRING_END;} //output to uart1
                #endif
            }
        #endif

        #if !DISABLE_LED //SK6812 LED
            //reorder color channels
            uint8_t colorArr[3];
            #if !DISABLE_SENSOR
                if (!ledTestColor){
                    colorArr[0] = (uint8_t)sensorColorArr[1]; //green
                    colorArr[1] = (uint8_t)sensorColorArr[0]; //red
                    colorArr[2] = (uint8_t)sensorColorArr[2]; //blue
                } else {
            #endif
                    if (++ledTestColorCur == 3){ledTestColorCur = 0;} //cycle next color
                    colorArr[0] = (ledTestColorCur==1)?ledTestColor:0U; //green
                    colorArr[1] = (ledTestColorCur==0)?ledTestColor:0U; //red
                    colorArr[2] = (ledTestColorCur==2)?ledTestColor:0U; //blue
            #if !DISABLE_SENSOR //done that way for readability
                }
            #endif

            //send data to led, NOTE: timing is far from perfect
            for (uint8_t colorIndex = 0; colorIndex < 3; colorIndex++){
                uint8_t color=colorArr[colorIndex];
                for (uint8_t bitLoop = 8; bitLoop > 0; bitLoop--){
                    if(color & 0b10000000){ //bit1
                        P55 = SET; //high
                        NOP(); NOP(); NOP(); NOP(); NOP(); NOP();
                        P55 = RESET; //low
                        NOP();NOP(); NOP(); NOP();
                    } else { //bit0
                        P55 = SET; //high
                        NOP(); NOP(); NOP(); NOP();
                        P55 = RESET; //low
                        NOP(); NOP(); NOP(); NOP(); NOP(); NOP();
                    }
                    color <<= 1;
                }    
            }
        #endif

        #if !(DISABLE_SENSOR && DISABLE_LED) && MCU_ENABLE_POWERDOWN_MODE
            if (!ledTestColor){ //not led test mode
                if (timer0_counter < WAKEUP_COUNT_LED_UPDATE){
                    uint16_t waitCycles = WAKEUP_COUNT_LED_UPDATE - timer0_counter;
                    RCC_SetPowerDownWakeupTimerCountdown(waitCycles); //try to match wanted update rate
                    //RCC_SetPowerDownMode(HAL_State_ON);
                    RCC_SetIdleMode(HAL_State_ON);
                }
            } else {
                RCC_SetPowerDownWakeupTimerCountdown(WAKEUP_COUNT_LED_TEST_CYCLE); //wait for next color cycle
                //RCC_SetPowerDownMode(HAL_State_ON);
                RCC_SetIdleMode(HAL_State_ON);
            }
            //NOP(); NOP(); NOP(); NOP();
            timer0_counter = 0U; //reset timer0 counter
        #else
            //limit update rate
            if (!ledTestColor){
                SYS_Delay(1000UL / LED_UPDATE_RATE);
            } else {
                SYS_Delay(LED_TEST_CYCLE_DURATION);
            }
        #endif

        #if !DISABLE_WATCHDOG
            WDT_ResetCounter(); //avoid hardware reset
        #endif
    }
}

