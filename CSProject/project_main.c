/*
     Computer systems final project
    by Niklas Raesalmi, Hieu Bui, Huan Vo

    Features:
    1. Sending morse to the terminal through the device: (Hieu, Niklas)
    Rotate the device to the left for '.', to the right for '-', and up or down for ' '.
    2. A button for fast sending the list of dots and dashes for 'SOS' message. (Niklas)
    3. 'Star Wars' theme playing when pressing the SOS button. (Huan)
    4. Light blinking when press the SOS button. The light pattern matches the message. (Niklas)
    5. Receiving text through uart as morse then using the buzzer to play the corresponding sound. '.' for short beep, '-' for long beep. (Hieu, Huan)
    6. Applying mpu sensor for gathering gyroscope and accelerometer data. (Hieu)
    7. State machine to handle running tasks concurrently (Huan)
*/

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
#include "sensors/buzzer.h"

#define NOTE_C5  523
#define NOTE_C6  1047
#define NOTE_F5  698
#define NOTE_F6  1397
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_AS4 466
#define NOTE_AS5 932
#define REST     0

#define PI 3.14159265

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char mpuTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];
Char buzzerTaskStack[STACKSIZE];
Char LEDTaskStack[STACKSIZE];

// Definition of the state machine
enum state { WAITING=1, DATA_READY };
enum state OPTState = WAITING;
enum state MPUState = WAITING;

// Global variables for MPU9250 data
float ax, ay, az, gx, gy, gz;
float roll;

// Most recent received status from sensorListener()
char temp = NULL;

// Boolean for checking button is pressed to send SOS signal
bool sendSOS = false;

// Morse list variable to store the list of morse character used for LED and printing to terminal
char morseList[15];

// The predefined list of characters that represent SOS message
const char SOS[15] = {'.', '.', '.', ' ', '-', '-', '-', ' ', '.', '.', '.', ' ', ' '};

// Buzzer buffer which contains the message from UART read
char beepMorse[64];

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

static PIN_Handle hBuzzer;
static PIN_State sBuzzer;
PIN_Config cBuzzer[] = {
  Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
  PIN_TERMINATE
};

// MPU I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};

// Function for handling button press, set sendSOS to true which send the SOS message
void buttonFxn(PIN_Handle handle, PIN_Id pinId) {
    sendSOS = true; // Send SOS signal
}



/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1) {
    // UART connection set up as 9600, 8n1
    UART_Handle uart;
    UART_Params uartParams;

    // Settings for UART
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.readTimeout = 1;
    uartParams.readMode = UART_MODE_BLOCKING;
    uartParams.baudRate = 9600; 
    uartParams.dataLength = UART_LEN_8; 
    uartParams.parityType = UART_PAR_NONE; 
    uartParams.stopBits = UART_STOP_ONE; 
    uartParams.readReturnMode = UART_RETURN_FULL;

    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL) {
       System_abort("Error opening the UART");
    }

    char morseLetter = NULL; // Last value received from MPU sensor
    char SOSchecker = NULL; // Last sent character
    char message[64]; // UART read buffer

    while (1) {
        // UART read for reading messages
        uint8_t bytesRead = UART_read(uart, message, sizeof(message)); // Read into the buffer, leaving space for null terminator
        if (bytesRead > 0) {
            strcpy(beepMorse, message);
            memset(message, 0, sizeof(message));
        }


        // sendSOS: First checks if it is needed to put spaces before the SOS signal,
        //    then use UART to send SOS signal
        if(sendSOS) {
            if(SOSchecker == '.' || SOSchecker == '-') {
                char space[10];
                sprintf(space, "%c\r\n\0", ' ');
                UART_write(uart, space ,strlen(space) + 1);
                UART_write(uart, space ,strlen(space) + 1);
                SOSchecker = NULL;
            }

            int i;
            char str0[10];
            for (i = 0; i < strlen(SOS); i++) {
                sprintf(str0, "%c\r\n\0", SOS[i]);
                UART_write(uart, str0, strlen(str0) +1);
            }

            Task_sleep(500000 / Clock_tickPeriod);
            sendSOS = false;
        }

        // Send sensor data as a string with UART if the state is DATA_READY
        if (MPUState == DATA_READY) {
            char str[200];
            char str1[10];

            morseLetter = sensorListener();
            if(morseLetter != temp) {
                if(morseLetter) {
                    sprintf(str1, "%c\r\n\0", morseLetter);
                    UART_write(uart, str1, strlen(str1) +1);
                    SOSchecker = morseLetter;
                }
                temp = morseLetter;
            }

            sprintf(str, "\nRoll: %.2f degrees\n"
                    "Gyroscope: gx=%.2f dps, gy=%.2f dps, gz=%.2f dps\n "
                    "Accelerometer: ax=%.2f m/s^2, ay=%.2f m/s^2, az=%.2f m/s^2\n",
                    roll, gx, gy, gz, ax, ay, az);
            System_printf("%s\n", str);
            System_flush();

            MPUState = WAITING;
        }

        // 500 milisecond sleep
        Task_sleep(500000 / Clock_tickPeriod);
    }
}

// Buzzer task function that constantly check if sendSOS is true 
// and if there is any dot or dash character in the first element of beepMorse.
Void buzzerFxn(UArg arg0, UArg arg1) {
    // Credit for melody https://github.com/hibit-dev/buzzer/tree/master/src/movies/star_wars

    int melody[] = {
      NOTE_AS4, NOTE_AS4, NOTE_AS4,
      NOTE_F5, NOTE_C6,
      NOTE_AS5, NOTE_A5, NOTE_G5, NOTE_F6, NOTE_C6,
      NOTE_AS5, NOTE_A5, NOTE_G5, NOTE_F6, NOTE_C6,
      NOTE_AS5, NOTE_A5, NOTE_AS5, NOTE_G5, NOTE_C5, NOTE_C5, NOTE_C5,
      NOTE_F5, NOTE_C6,
      NOTE_AS5, NOTE_A5, NOTE_G5, NOTE_F6, NOTE_C6,
    };

    int durations[] = {
      8, 8, 8,
      2, 2,
      8, 8, 8, 2, 4,
      8, 8, 8, 2, 4,
      8, 8, 8, 2, 8, 8, 8,
      2, 2,
      8, 8, 8, 2, 4,
    };
    while(1) {
        if(sendSOS) {
            strcpy(morseList, SOS);
            buzzerOpen(hBuzzer);
            int size = sizeof(durations) / sizeof(int);
            int note;
              for (note = 0; note < size; note++) {
                int duration = 1000 / durations[note];
                buzzerSetFrequency(melody[note]);

                int pauseBetweenNotes = duration * 1.30;
                Task_sleep(pauseBetweenNotes*100);
              }
            buzzerClose();
        }

        if (beepMorse[0] == '.' || beepMorse[0] == '-') {

            int i;
            for (i = 0; i < strlen(beepMorse); i++) {
                System_printf(beepMorse[i]);
                buzzerOpen(hBuzzer);

                // Handle dash (-)
                if (beepMorse[i] == '-') {
                    buzzerSetFrequency(1000);
                    Task_sleep(300000 / Clock_tickPeriod);
                    buzzerSetFrequency(0);
                }
                // Handle dot (.)
                else if (beepMorse[i] == '.') {
                    buzzerSetFrequency(1000);
                    Task_sleep(100000 / Clock_tickPeriod);
                    buzzerSetFrequency(0);
                }
                // Handle carriage return or newline
                else if (beepMorse[i] == '\x0d' || beepMorse[i] == '\x0a') {
                    Task_sleep(50000 / Clock_tickPeriod);
                }
                // Break if any other character is encountered
                else {
                    break;
                }
                buzzerClose();

            }

            // Clear beepMorse array once all processing is done
            for (i = 0; i < strlen(beepMorse); i++) {
                beepMorse[i] = '\0';
            }
        }
    }
}

// MPU sensor task for collecting data and store them to global variables
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
            MPUState = DATA_READY;
        }

        // The sensor receive data 20 times per second
        Task_sleep(50000 / Clock_tickPeriod);
    }
}

// Function for returning morse character when condition met using sensor data
char sensorListener() {
    if(roll > 60 && roll < 120) {
        return '-';
    }
    if(roll < -60 && roll > -120) {
        return '.';
    }
    if(az > 1.25 || az < -1.25) {
        return ' ';
    }
    return NULL;
}

// LED task function for blinking a received list of characters
Void LEDFxn(UArg arg0, UArg arg1) {
    while(1) {
        if(morseList[0] == '.' || morseList[0] == '-') {
            int i;
            for(i=0;i< sizeof(morseList); i++){
                if (morseList[i] == ' ') Task_sleep(700000 / Clock_tickPeriod);
                else {
                    uint_t pinValue = PIN_getOutputValue(Board_LED0);
                    pinValue = !pinValue;
                    PIN_setOutputValue(ledHandle, Board_LED0, pinValue);
                    if(morseList[i] == '.') Task_sleep(100000 / Clock_tickPeriod);
                    if(morseList[i] == '-') Task_sleep(300000 / Clock_tickPeriod);
                    pinValue = !pinValue;
                    PIN_setOutputValue(ledHandle, Board_LED0, pinValue);
                    Task_sleep(100000 / Clock_tickPeriod);
                }
                morseList[i] = NULL;
            }
        }
        Task_sleep(200000 / Clock_tickPeriod);
    }
}

Int main(void) {
    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;
    Task_Handle mpuSensorTaskHandle;
    Task_Params mpuSensorTaskParams;
    Task_Handle buzzerTaskHandle;
    Task_Params buzzerTaskParams;
    Task_Handle LEDTaskHandle;
    Task_Params LEDTaskParams;

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

    // Open LED pins and initialize the LED in the program
    ledHandle = PIN_open(&ledState, ledConfig);
    if (!ledHandle) {
       System_abort("Error initializing LED pin\n");
    }

    //Open buzzer pins
    hBuzzer = PIN_open(&sBuzzer, cBuzzer);
    if (hBuzzer == NULL) {
        System_abort("Pin open failed!");
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
    mpuSensorTaskParams.priority = 3;
    mpuSensorTaskHandle = Task_create(mpuSensorTaskFxn, &mpuSensorTaskParams, NULL);
    if (mpuSensorTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* Buzzer task */
    Task_Params_init(&buzzerTaskParams);
    buzzerTaskParams.stackSize = STACKSIZE;
    buzzerTaskParams.stack = &buzzerTaskStack;
    buzzerTaskParams.priority=1;
    buzzerTaskHandle = Task_create(buzzerFxn, &buzzerTaskParams, NULL);
    if (buzzerTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* LED task */
    Task_Params_init(&LEDTaskParams);
    LEDTaskParams.stackSize = STACKSIZE;
    LEDTaskParams.stack = &LEDTaskStack;
    LEDTaskParams.priority=1;
    LEDTaskHandle = Task_create(LEDFxn, &LEDTaskParams, NULL);
    if (LEDTaskHandle == NULL) {
        System_abort("Task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
