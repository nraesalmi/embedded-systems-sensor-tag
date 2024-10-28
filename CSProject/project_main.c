/* C Standard library */
#include <stdio.h>

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

/* Board Header files */
#include "Board.h"
#include "sensors/opt3001.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];

// Definition of the state machine
enum state { WAITING=1, DATA_READY };
enum state programState = WAITING;

// Global variable for ambient light
double ambientLight = -1000.0;

// Pins RTOS-variables and configuration
static PIN_Handle buttonHandle;
static PIN_State buttonState;
static PIN_Handle ledHandle;
static PIN_State ledState;

PIN_Config buttonConfig[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE // The configuration table is always terminated with this constant
};

PIN_Config ledConfig[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE // The configuration table is always terminated with this constant
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
        if (programState == DATA_READY) {
            char str[100];
            sprintf(str, "%f ", ambientLight);
            System_printf("Ambient Light: %s\n", str);
            System_flush();
            UART_write(uart, str, strlen(str));
            programState = WAITING;
        }
        // Twice per second, you can modify this
        Task_sleep(500000 / Clock_tickPeriod);
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1) {

    I2C_Handle      i2c;
    I2C_Params      i2cParams;

    // Open the i2c bus
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;


    i2c = I2C_open(Board_I2C_TMP, &i2cParams);
    if (i2c == NULL) {
       System_abort("Error Initializing I2C\n");
    }

    // Setup the OPT3001 sensor for use
    Task_sleep(100000 / Clock_tickPeriod);
    opt3001_setup(&i2c);

    while (1) {

        // Save the sensor value into the global variable and edit state
        if (programState == WAITING) {
            double data = opt3001_get_data(&i2c);
            ambientLight = data;
            programState = DATA_READY;
        }

        // Twice per second, you can modify this
        Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Int main(void) {


    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    // Initialize board
    Board_initGeneral();

    //Initialize i2c bus
    Board_initI2C();

    // Initialize UART
    Board_initUART();

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

    /* Task */
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority=1;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority=2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
