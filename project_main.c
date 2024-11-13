/* C Standard library */
#include <stdio.h>
#include <math.h>

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/i2c/I2CCC26XX.h>


/* Board Header files */
#include "Board.h"
#include "sensors/opt3001.h"
#include "sensors/mpu9250.h"

#define PI 3.14159265

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char mpuTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];

// Definition of the state machine
enum state { WAITING=1, DATA_READY };
enum state OPTState = WAITING;
enum state MPUState = WAITING;


// Global variable for opt3001 data
double ambientLight = -1000.0;

// Global variables for MPU9250 data
float ax, ay, az, gx, gy, gz;
float roll, pitch;

// Turn variable for i2c handling
char turn = 'j';

char temp = NULL;


// Pins RTOS-variables and configuration
static PIN_Handle buttonHandle;
static PIN_State buttonState;
static PIN_Handle ledHandle;
static PIN_State ledState;
static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;

// MPU power pin
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

//Configs
PIN_Config buttonConfig[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE // The configuration table is always terminated with this constant
};

PIN_Config ledConfig[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE // The configuration table is always terminated with this constant
};

// MPU I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};

void buttonFxn(PIN_Handle handle, PIN_Id pinId) {

    // Blink led on the device
    uint_t pinValue = PIN_getOutputValue(Board_LED0);
    pinValue = !pinValue;
    PIN_setOutputValue(ledHandle, Board_LED0, pinValue);
}

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {

    // UART connection set up as 9600, 8n1
    UART_Handle uart;
    UART_Params uartParams;

    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readMode = UART_MODE_BLOCKING;
    uartParams.baudRate = 9600; // 9600 baud rate
    uartParams.dataLength = UART_LEN_8; // 8
    uartParams.parityType = UART_PAR_NONE; // n
    uartParams.stopBits = UART_STOP_ONE; // 1

    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL) {
       System_abort("Error opening the UART");
    }

    while (1) {
        // Send sensor data as a string with UART if the state is DATA_READY
        if (MPUState == DATA_READY) {
            char str[200];
            char str1[10];

            char morseLetter = sensorListener();
            if(morseLetter != temp) {
                if(morseLetter) {
                    sprintf(str1, "%c\r\n", morseLetter);
                    sprintf(str, "Roll: %.2f degrees\r\nPitch: %.2f degrees\r\n"
                            "Gyroscope: gx=%.2f dps, gy=%.2f dps, gz=%.2f dps\r\n"
                            "Accelerometer: ax=%.2f m/s^2, ay=%.2f m/s^2, az=%.2f m/s^2\r\n",
                            roll, pitch, gx, gy, gz, ax, ay, az);
                    UART_write(uart, str1, strlen(str1) +1);
                }
                temp = morseLetter;
            }
            System_printf("%s\n", str);
            System_flush();
            //OPTState = WAITING;
            MPUState = WAITING;
        }
        // Twice per second, you can modify this
        Task_sleep(500000 / Clock_tickPeriod);
    }
}

//Void sensorTaskFxn(UArg arg0, UArg arg1) {
//
//    I2C_Handle      i2c;
//    I2C_Params      i2cParams;
//
//    // Open the i2c bus
//    I2C_Params_init(&i2cParams);
//    i2cParams.bitRate = I2C_400kHz;
//
//    while (1) {
//
//        // Save the sensor value into the global variable and edit state
//        if (OPTState == WAITING && turn == 'i') {
//            i2c = I2C_open(Board_I2C_TMP, &i2cParams);
//            if (i2c == NULL) {
//               System_abort("Error Initializing I2C\n");
//            }
//
//            // Setup the OPT3001 sensor for use
//            opt3001_setup(&i2c);
//            Task_sleep(650000/ Clock_tickPeriod);
//
//            ambientLight = opt3001_get_data(&i2c);
//            OPTState = DATA_READY;
//            I2C_close(i2c);
//            turn = 'j';
//
//        }
//
//        // Twice per second, you can modify this
//        Task_sleep(50000 / Clock_tickPeriod);
//    }
//}

Void mpuSensorTaskFxn(UArg arg0, UArg arg1) {

    I2C_Handle      i2cMPU;
    I2C_Params      i2cMPUParams;

    // Open the i2c bus
    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

    // MPU power on
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

    // Wait 20ms for the MPU sensor to power up
    Task_sleep(20000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // Open MPU i2c
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
       System_abort("Error Initializing I2C for MPU\n");
    }

    // Setup the MPU9250 sensor for use
    Task_sleep(20000 / Clock_tickPeriod);
    mpu9250_setup(&i2cMPU);

    while (1) {

        // Save the sensor value into the global variable and edit state
        if (MPUState == WAITING) {

            mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);
            roll = atan2(ay, az) * 180.0 / PI;
            pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
            MPUState = DATA_READY;

        }

        // Twice per second, you can modify this
        Task_sleep(50000 / Clock_tickPeriod);
    }

    //I2C_close(i2cMPU);
    //PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_OFF);
    //turn = 'i';
}

char sensorListener() {
    if(roll > 45 && roll < 135) {
        return '-';
    }
    if(roll < -45 && roll > -135) {
        return '.';
    }
    return NULL;
}

Int main(void) {
    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;
    Task_Handle mpuSensorTaskHandle;
    Task_Params mpuSensorTaskParams;

    // Initialize board
    Board_initGeneral();

    //Initialize i2c bus
    Board_initI2C();

    // Initialize UART
    Board_initUART();

    // Open MPU power pin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
        System_abort("Pin open failed!");
    }

    // Open the button and led pins
    // Register interrupt handler for button
    // Initialize the LED in the program
    ledHandle = PIN_open(&ledState, ledConfig);
    if (!ledHandle) {
       System_abort("Error initializing LED pin\n");
    }

    // Initialize the button in the program
    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if (!buttonHandle) {
       System_abort("Error initializing button pin\n");
    }

    // Register the button interrupt handler
    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0) {
       System_abort("Error registering button callback function");
    }

    //Light sensor task
//    Task_Params_init(&sensorTaskParams);
//    sensorTaskParams.stackSize = STACKSIZE;
//    sensorTaskParams.stack = &sensorTaskStack;
//    sensorTaskParams.priority=1;
//    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
//    if (sensorTaskHandle == NULL) {
//        System_abort("Task create failed!");
//    }

    /* UART task */
    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority=2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* MPU9250 sensor task */
    Task_Params_init(&mpuSensorTaskParams);
    mpuSensorTaskParams.stackSize = STACKSIZE;
    mpuSensorTaskParams.stack = &mpuTaskStack;
    mpuSensorTaskParams.priority = 1;
    mpuSensorTaskHandle = Task_create(mpuSensorTaskFxn, &mpuSensorTaskParams, NULL);
    if (mpuSensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
