/*
 * ServoEasing.hpp
 *
 *  Enables smooth movement from one servo position to another.
 *  Linear as well as other ease movements (e.g. cubic) for all servos attached to the Arduino Servo library are provided.
 *  Interface is in degree but internally only microseconds (if using Servo library) or units (if using PCA9685 expander) are used,
 *  since the resolution is better and we avoid the map function on every Servo.write().
 *  The blocking functions wait for 20 ms since this is the default refresh time of the used Servo library.
 *
 *  The AVR Servo library supports only one timer, which means not more than 12 servos are supported using this library.
 *
 *  Copyright (C) 2019-2022  Armin Joachimsmeyer
 *  armin.joachimsmeyer@gmail.com
 *
 *  This file is part of ServoEasing https://github.com/ArminJo/ServoEasing.
 *
 *  ServoEasing is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/gpl.html>.
 */

/*
 * This library can be configured at compile time by the following options / macros:
 * For more details see: https://github.com/ArminJo/ServoEasing#compile-options--macros-for-this-library
 *
 * - USE_PCA9685_SERVO_EXPANDER         Enables the use of the PCA9685 I2C expander chip/board.
 * - USE_SERVO_LIB                      Use of PCA9685 normally disables use of regular servo library. You can force additional using of regular servo library by defining USE_SERVO_LIB.
 * - USE_LEIGHTWEIGHT_SERVO_LIB         Makes the servo pulse generating immune to other libraries blocking interrupts for a longer time like SoftwareSerial, Adafruit_NeoPixel and DmxSimple.
 * - PROVIDE_ONLY_LINEAR_MOVEMENT       Disables all but LINEAR movement. Saves up to 1540 bytes program memory.
 * - DISABLE_COMPLEX_FUNCTIONS          Disables the SINE, CIRCULAR, BACK, ELASTIC and BOUNCE easings.
 * - MAX_EASING_SERVOS                  Saves 4 byte RAM per servo.
 * - DISABLE_MICROS_AS_DEGREE_PARAMETER Disables passing also microsecond values as (target angle) parameter. Saves 128 bytes program memory.
 * - PRINT_FOR_SERIAL_PLOTTER           Generate serial output for Arduino Plotter (Ctrl-Shift-L).
 */

#ifndef _SERVO_EASING_HPP
#define _SERVO_EASING_HPP

#include <Arduino.h>

#include "ServoEasing.h"

#if defined(USE_LEIGHTWEIGHT_SERVO_LIB) && (defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__))
#include "LightweightServo.hpp" // include sources of LightweightServo library
#endif

#if defined(ESP8266) || defined(ESP32)
#include "Ticker.h" // for ServoEasingInterrupt functions
Ticker Timer20ms;

// BluePill in 2 flavors
#elif defined(STM32F1xx)   // for "Generic STM32F1 series / STM32:stm32" from STM32 Boards from STM32 cores of Arduino Board manager
// https://github.com/stm32duino/BoardManagerFiles/raw/master/STM32/package_stm_index.json
#include <HardwareTimer.h> // 4 timers and 3. timer is used for tone(), 2. for Servo
/*
 * Use timer 4 as IRMP timer.
 * Timer 4 blocks PB6, PB7, PB8, PB9, so if you require one of them as Servo output, you must choose another timer.
 */
HardwareTimer Timer20ms(TIM4);

#elif defined(__STM32F1__) // or ARDUINO_ARCH_STM32F1 for "Generic STM32F103C series / stm32duino:STM32F1" from STM32F1 Boards (STM32duino.com) of Arduino Board manager
// http://dan.drown.org/stm32duino/package_STM32duino_index.json
#include <HardwareTimer.h>
#  if defined(STM32_HIGH_DENSITY)
HardwareTimer Timer20ms(7);  // 8 timers and 8. timer is used for tone()
#  else
/*
 * Use timer 3 for ServoEasingInterrupt functions.
 * Timer 3 blocks PA6, PA7, PB0, PB1, so if you required one of them as Servo output, you must choose another timer.
 */
HardwareTimer Timer20ms(3);  // 4 timers and 4. timer is used for tone()
#  endif

#elif defined(__SAM3X8E__)  // Arduino DUE
/*
 * Timer 0 to 5 are used by Servo library (by defining handlers)
 *
 * Timer 6 is TC2 channel 0
 * Timer 7 is TC2 channel 1
 * Timer 8 is TC2 channel 2
 *
 * We use timer 8 here
 */
#define TC_FOR_20_MS_TIMER      TC2
#define CHANNEL_FOR_20_MS_TIMER 2
#define ID_TC_FOR_20_MS_TIMER   ID_TC8 // Timer 8 is TC2 channel 2
#define IRQn_FOR_20_MS_TIMER    TC8_IRQn
#define HANDLER_FOR_20_MS_TIMER TC8_Handler

#elif defined(ARDUINO_ARCH_MBED) // Arduino Nano 33 BLE + Sparkfun Apollo3
mbed::Ticker Timer20ms;

/*************************************************************************************************************************************
 * RP2040 based boards for pico core
 * https://github.com/earlephilhower/arduino-pico
 * https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
 * Can use any pin for PWM, no timer restrictions
 *************************************************************************************************************************************/
#elif defined(ARDUINO_ARCH_RP2040) // Raspberry Pi Pico, Adafruit Feather RP2040, etc.
#include "pico/time.h"
repeating_timer_t Timer20ms;
void handleServoTimerInterrupt();
// The timer callback has a parameter and a return value
bool handleServoTimerInterruptHelper(repeating_timer_t*) {
    handleServoTimerInterrupt();
    return true;
}

#elif defined(TEENSYDUINO)
// common for all Teensy
IntervalTimer Timer20ms;
#endif

/*
 * Enable this to see information on each call.
 * Since there should be no library which uses Serial, enable it only for development purposes.
 */
//#define TRACE
//#define DEBUG
// Propagate debug level
#if defined(TRACE)
#define DEBUG
#endif

volatile bool ServoEasing::sInterruptsAreActive = false; // true if interrupts are still active, i.e. at least one Servo is moving with interrupts.

/*
 * list to hold all ServoEasing Objects in order to move them together
 * Cannot use "static servo_t servos[MAX_SERVOS];" from Servo library since it is static :-(
 */
uint_fast8_t ServoEasing::sServoArrayMaxIndex = 0; // maximum index of an attached servo in ServoEasing::ServoEasingArray[]
ServoEasing *ServoEasing::ServoEasingArray[MAX_EASING_SERVOS];
/*
 * Used exclusively for *ForAllServos() functions. Is updated by write() or startEaseToD() function, to keep it synchronized.
 * Can contain degree values or microseconds but not units.
 * Use int since we want to support negative degree values (with trim)
 */
int ServoEasing::ServoEasingNextPositionArray[MAX_EASING_SERVOS];

const char easeTypeLinear[] PROGMEM = "linear";
#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
const char easeTypeQuadratic[] PROGMEM = "quadratic";
const char easeTypeCubic[] PROGMEM = "cubic";
const char easeTypeQuartic[] PROGMEM = "quartic";
const char easeTypePrecision[] PROGMEM = "precision";
const char easeTypeUser[] PROGMEM = "user";
const char easeTypeNotDefined[] PROGMEM = "";
const char easeTypeDummy[] PROGMEM = "dummy";
#  if !defined(DISABLE_COMPLEX_FUNCTIONS)
const char easeTypeSine[] PROGMEM = "sine";
const char easeTypeCircular[] PROGMEM = "circular";
const char easeTypeBack[] PROGMEM = "back";
const char easeTypeElastic[] PROGMEM = "elastic";
const char easeTypeBounce[] PROGMEM = "bounce";
#  endif
#endif

const char *const easeTypeStrings[] PROGMEM = { easeTypeLinear
#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
        , easeTypeQuadratic, easeTypeCubic, easeTypeQuartic, easeTypePrecision, easeTypeUser, easeTypeNotDefined,
        easeTypeNotDefined,
#  if !defined(DISABLE_COMPLEX_FUNCTIONS)
        easeTypeSine, easeTypeCircular, easeTypeBack, easeTypeElastic, easeTypeBounce
#  endif
#endif
        };

#if defined(USE_PCA9685_SERVO_EXPANDER)
//#define USE_SOFT_I2C_MASTER // Saves 2110 bytes program memory and 200 bytes RAM compared with Arduino Wire
#  if defined(USE_SOFT_I2C_MASTER)
#include "SoftI2CMasterConfig.h"
#include "SoftI2CMaster.h"
#  endif // defined(USE_SOFT_I2C_MASTER)

#  if !defined _BV
#  define _BV(bit) (1 << (bit))
#  endif
// Constructor with I2C address required
#if defined(USE_SOFT_I2C_MASTER)
ServoEasing::ServoEasing(uint8_t aPCA9685I2CAddress) // @suppress("Class members should be properly initialized")
#else
ServoEasing::ServoEasing(uint8_t aPCA9685I2CAddress, TwoWire *aI2CClass) // @suppress("Class members should be properly initialized")
#endif
{
    mPCA9685I2CAddress = aPCA9685I2CAddress;
#if !defined(USE_SOFT_I2C_MASTER)
    mI2CClass = aI2CClass;
#endif

    // On an ESP8266 it was NOT initialized to 0 :-(.
    mTrimMicrosecondsOrUnits = 0;
    mSpeed = START_EASE_TO_SPEED;
    mServoMoves = false;
    mOperateServoReverse = false;

#if defined(USE_SERVO_LIB)
    mServoIsConnectedToExpander = true;
#endif
#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
    mEasingType = EASE_LINEAR;
    mUserEaseInFunction = NULL;
#endif

#if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    pinMode(TIMING_OUTPUT_PIN, OUTPUT);
#endif
}

void ServoEasing::I2CInit() {
// Initialize I2C
#if defined(USE_SOFT_I2C_MASTER)
    i2c_init(); // Initialize everything and check for bus lockup
#else
    mI2CClass->begin();
    mI2CClass->setClock(I2C_CLOCK_FREQUENCY); // 1000000 does not work for me, maybe because of parasitic breadboard capacities
#  if defined (ARDUINO_ARCH_AVR) // Other platforms do not have this new function
    mI2CClass->setWireTimeout(); // Sets default timeout of 25 ms.
#  endif
#endif
}
/*
 * Initialize I2C and software reset all PCA9685 expanders
 */
void ServoEasing::PCA9685Reset() {
    // Send software reset to expander(s)
#if defined(USE_SOFT_I2C_MASTER)
    i2c_start(PCA9685_GENERAL_CALL_ADDRESS << 1);
    i2c_write(PCA9685_SOFTWARE_RESET);
    i2c_stop();
#else
    mI2CClass->beginTransmission(PCA9685_GENERAL_CALL_ADDRESS);
    mI2CClass->write(PCA9685_SOFTWARE_RESET);
    mI2CClass->endTransmission();
#endif
}

/*
 * Set expander to 20 ms period for 4096-part cycle and wait 2 milliseconds
 * This results in a resolution of 4.88 �s per step.
 */
void ServoEasing::PCA9685Init() {
    // Set expander to 20 ms period
    I2CWriteByte(PCA9685_MODE1_REGISTER, _BV(PCA9685_MODE_1_SLEEP)); // go to sleep
    I2CWriteByte(PCA9685_PRESCALE_REGISTER, PCA9685_PRESCALER_FOR_20_MS); // set the prescaler
    I2CWriteByte(PCA9685_MODE1_REGISTER, _BV(PCA9685_MODE_1_AUTOINCREMENT)); // reset sleep and enable auto increment
    delay(2); // > 500 �s according to datasheet
}

void ServoEasing::I2CWriteByte(uint8_t aAddress, uint8_t aData) {
#if defined(USE_SOFT_I2C_MASTER)
    i2c_start(mPCA9685I2CAddress << 1);
    i2c_write(aAddress);
    i2c_write(aData);
    i2c_stop();
#else
    mI2CClass->beginTransmission(mPCA9685I2CAddress);
    mI2CClass->write(aAddress);
    mI2CClass->write(aData);
#  if defined(DEBUG)
    uint8_t tWireReturnCode = mI2CClass->endTransmission();
    if (tWireReturnCode != 0) {
        // I have seen this at my ESP32 module :-( - but it is no buffer overflow.
        Serial.print((char) (tWireReturnCode + '0')); // Error enum i2c_err_t: I2C_ERROR_ACK = 2, I2C_ERROR_TIMEOUT = 3
    }
#  else
    mI2CClass->endTransmission();
#  endif
#endif
}

/**
 * @param aPWMValueAsUnits - The point in the 4096-part cycle, where the output goes OFF (LOW). On is fixed at 0.
 * Useful values are from 111 (111.411 = 544 �s) to 491 (491.52 = 2400 �s)
 * This results in an resolution of approximately 0.5 degree.
 * 4096 means output is signal fully off
 */
void ServoEasing::setPWM(uint16_t aPWMOffValueAsUnits) {
#if defined(USE_SOFT_I2C_MASTER)
    i2c_start(mPCA9685I2CAddress << 1);
    i2c_write((PCA9685_FIRST_PWM_REGISTER + 2) + 4 * mServoPin);
    i2c_write(aPWMOffValueAsUnits);
    i2c_write(aPWMOffValueAsUnits >> 8);
    i2c_stop();
#else
    mI2CClass->beginTransmission(mPCA9685I2CAddress);
    // +2 since we we do not set the begin value, it is fixed at 0
    mI2CClass->write((PCA9685_FIRST_PWM_REGISTER + 2) + 4 * mServoPin);
    mI2CClass->write(aPWMOffValueAsUnits);
    mI2CClass->write(aPWMOffValueAsUnits >> 8);
#  if defined(DEBUG) && not defined(ESP32)
    // The ESP32 I2C interferes with the Ticker / Timer library used.
    // Even with 100 kHz clock we have some dropouts / NAK's because of sending address again instead of first data.
    uint8_t tWireReturnCode = mI2CClass->endTransmission();
    if (tWireReturnCode != 0) {
        // If you end up here, maybe the second module is not attached?
        Serial.print((char) (tWireReturnCode + '0'));    // Error enum i2c_err_t: I2C_ERROR_ACK = 2, I2C_ERROR_TIMEOUT = 3
    }
#  else
    mI2CClass->endTransmission();
#  endif
#endif
}

/**
 * Here you can specify an on/start value for the pulse in order not to start all pulses at the same time.
 * Is used by writeMicrosecondsOrUnits() with onValue as mServoPin * 235
 * Requires 550 �s to send data => 8.8 ms for 16 Servos, 17.6 ms for 32 servos. => more than 2 expander boards
 * cannot be connected to one I2C bus, if all servos must be able to move simultaneously.
 */
void ServoEasing::setPWM(uint16_t aPWMOnStartValueAsUnits, uint16_t aPWMPulseDurationAsUnits) {
#if defined(USE_SOFT_I2C_MASTER)
    i2c_start(mPCA9685I2CAddress << 1);
    i2c_write((PCA9685_FIRST_PWM_REGISTER) + 4 * mServoPin);
    i2c_write(aPWMOnStartValueAsUnits);
    i2c_write(aPWMOnStartValueAsUnits >> 8);
    i2c_write(aPWMOnStartValueAsUnits + aPWMPulseDurationAsUnits);
    i2c_write((aPWMOnStartValueAsUnits + aPWMPulseDurationAsUnits) >> 8);
    i2c_stop();
#else
    mI2CClass->beginTransmission(mPCA9685I2CAddress);
    mI2CClass->write((PCA9685_FIRST_PWM_REGISTER) + 4 * mServoPin);
    mI2CClass->write(aPWMOnStartValueAsUnits);
    mI2CClass->write(aPWMOnStartValueAsUnits >> 8);
    mI2CClass->write(aPWMOnStartValueAsUnits + aPWMPulseDurationAsUnits);
    mI2CClass->write((aPWMOnStartValueAsUnits + aPWMPulseDurationAsUnits) >> 8);
#  if defined(DEBUG) && not defined(ESP32)
    // The ESP32 I2C interferes with the Ticker / Timer library used.
    // Even with 100 kHz clock we have some dropouts / NAK's because of sending address again instead of first data.
    uint8_t tWireReturnCode = mI2CClass->endTransmission();    // blocking call
    if (tWireReturnCode != 0) {
        // If you end up here, maybe the second module is not attached?
        Serial.print((char) (tWireReturnCode + '0'));    // Error enum i2c_err_t: I2C_ERROR_ACK = 2, I2C_ERROR_TIMEOUT = 3
    }
#  else
    mI2CClass->endTransmission();
#  endif
#endif
}

int ServoEasing::MicrosecondsToPCA9685Units(int aMicroseconds) {
    /*
     * 4096 units per 20 milliseconds => aMicroseconds / 4.8828
     */
    return ((4096L * aMicroseconds) / REFRESH_INTERVAL_MICROS);
}

int ServoEasing::PCA9685UnitsToMicroseconds(int aPCA9685Units) {
    /*
     * 4096 units per 20 milliseconds => aPCA9685Units * 4.8828
     * (aPCA9685Units * 625) / 128 use int32_t to avoid overflow
     */
    return ((int32_t) aPCA9685Units * (REFRESH_INTERVAL_MICROS / 32)) / (4096 / 32);
}

#endif // defined(USE_PCA9685_SERVO_EXPANDER)

// Constructor without I2C address
ServoEasing::ServoEasing() // @suppress("Class members should be properly initialized")
#if !defined(_DO_NOT_USE_SERVO_LIB)
:
        Servo()
#endif
{
    // On an ESP8266 it was NOT initialized to 0 :-(.
    mTrimMicrosecondsOrUnits = 0;
    mSpeed = START_EASE_TO_SPEED;
    mServoMoves = false;
    mOperateServoReverse = false;

#if defined(USE_SERVO_LIB)
    mServoIsConnectedToExpander = false;
#endif
#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
    mEasingType = EASE_LINEAR;
    mUserEaseInFunction = NULL;
#endif
    TargetPositionReachedHandler = NULL;

#if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    pinMode(TIMING_OUTPUT_PIN, OUTPUT);
#endif
}

/*
 * Specify the microseconds values for 0 and 180 degree for the servo.
 * The values can be determined by the EndPositionsTest example.
 *
 * If USE_LEIGHTWEIGHT_SERVO_LIB is enabled:
 *      Return 0/false if not pin 9 or 10 else return aPin
 *      Pin number != 9 results in using pin 10.
 * If USE_PCA9685_SERVO_EXPANDER is enabled:
 *      Return true only if channel number is between 0 and 15 since PCA9685 has only 16 channels, else returns false
 * Else return servoIndex / internal channel number
 */
uint8_t ServoEasing::attach(int aPin) {
    return attach(aPin, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE);
}

// Here no units accepted, only microseconds!
uint8_t ServoEasing::attach(int aPin, int aMicrosecondsForServo0Degree, int aMicrosecondsForServo180Degree) {
    return attach(aPin, aMicrosecondsForServo0Degree, aMicrosecondsForServo180Degree, 0, 180);
}

/*
 * Combination of attach with initial write.
 */
uint8_t ServoEasing::attach(int aPin, int aInitialDegreeOrMicrosecond) {
    return attach(aPin, aInitialDegreeOrMicrosecond, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE);
}

/*
 * Specify the start value written to the servo and the microseconds values for 0 and 180 degree for the servo.
 * The values can be determined by the EndPositionsTest example.
 */
uint8_t ServoEasing::attach(int aPin, int aInitialDegreeOrMicrosecond, int aMicrosecondsForServo0Degree,
        int aMicrosecondsForServo180Degree) {
    return attach(aPin, aInitialDegreeOrMicrosecond, aMicrosecondsForServo0Degree, aMicrosecondsForServo180Degree, 0, 180);
}

/*
 * The microseconds values at aServoLowDegree and aServoHighDegree are used to compute the microseconds values at 0 and 180 degrees
 * and can be used e.g. to run the servo from virtual -90 to +90 degree (See TwoServos example).
 */
uint8_t ServoEasing::attach(int aPin, int aInitialDegreeOrMicrosecond, int aMicrosecondsForServoLowDegree,
        int aMicrosecondsForServoHighDegree, int aServoLowDegree, int aServoHighDegree) {
    uint8_t tReturnValue = attach(aPin, aMicrosecondsForServoLowDegree, aMicrosecondsForServoHighDegree, aServoLowDegree,
            aServoHighDegree);
    write(aInitialDegreeOrMicrosecond);
    return tReturnValue;
}

/**
 * Attaches servo to pin and sets the servo timing parameters.
 * @param aMicrosecondsForServoLowDegree no units accepted, only microseconds!
 * @param aServoLowDegree can be negative. For this case an appropriate trim value is added, since this is the only way to handle negative values.
 * @return If USE_LEIGHTWEIGHT_SERVO_LIB is enabled:
 *             Return 0/false if not pin 9 or 10 else return aPin
 *             Pin number != 9 results in using pin 10.
 *         Else return servoIndex / internal channel number
 */
uint8_t ServoEasing::attach(int aPin, int aMicrosecondsForServoLowDegree, int aMicrosecondsForServoHighDegree, int aServoLowDegree,
        int aServoHighDegree) {
    /*
     * Get the 0 and 180 degree values.
     */
    int tMicrosecondsForServo0Degree = map(0, aServoLowDegree, aServoHighDegree, aMicrosecondsForServoLowDegree,
            aMicrosecondsForServoHighDegree);
    int tMicrosecondsForServo180Degree = map(180, aServoLowDegree, aServoHighDegree, aMicrosecondsForServoLowDegree,
            aMicrosecondsForServoHighDegree);

    mServoPin = aPin;
#if defined(USE_PCA9685_SERVO_EXPANDER)
#  if defined(USE_SERVO_LIB)
    if (mServoIsConnectedToExpander) {
        mServo0DegreeMicrosecondsOrUnits = MicrosecondsToPCA9685Units(tMicrosecondsForServo0Degree);
        mServo180DegreeMicrosecondsOrUnits = MicrosecondsToPCA9685Units(tMicrosecondsForServo180Degree);
    } else {
        mServo0DegreeMicrosecondsOrUnits = tMicrosecondsForServo0Degree;
        mServo180DegreeMicrosecondsOrUnits = tMicrosecondsForServo180Degree;
    }
#  else
    mServo0DegreeMicrosecondsOrUnits = MicrosecondsToPCA9685Units(tMicrosecondsForServo0Degree);
    mServo180DegreeMicrosecondsOrUnits = MicrosecondsToPCA9685Units(tMicrosecondsForServo180Degree);
#  endif
#else
    mServo0DegreeMicrosecondsOrUnits = tMicrosecondsForServo0Degree;
    mServo180DegreeMicrosecondsOrUnits = tMicrosecondsForServo180Degree;
#endif

    /*
     * Now put this servo instance into list of servos
     */
    uint8_t tReturnValue = INVALID_SERVO; // flag indicating an invalid servo index
    for (uint_fast8_t tServoIndex = 0; tServoIndex < MAX_EASING_SERVOS; ++tServoIndex) {
        if (ServoEasingArray[tServoIndex] == NULL) {
            ServoEasingArray[tServoIndex] = this;
            tReturnValue = tServoIndex;
            if (tServoIndex > sServoArrayMaxIndex) {
                sServoArrayMaxIndex = tServoIndex;
            }
            break;
        }
    }
    mServoIndex = tReturnValue;

#if defined(TRACE)
    Serial.print("Index=");
    Serial.print(tReturnValue);
    Serial.print(" pin=");
    Serial.print(mServoPin);
    Serial.print(" low=");
    Serial.print(aServoLowDegree);
    Serial.print('|');
    Serial.print(aMicrosecondsForServoLowDegree);
    Serial.print(" high=");
    Serial.print(aServoHighDegree);
    Serial.print('|');
    Serial.print(aMicrosecondsForServoHighDegree);
    Serial.print(' ');
    printStatic(&Serial);
#endif

#if defined(USE_PCA9685_SERVO_EXPANDER)
    mCurrentMicrosecondsOrUnits = DEFAULT_PCA9685_UNITS_FOR_90_DEGREE; // The start value if we forget the initial write()
#  if defined(USE_SERVO_LIB)
    if (mServoIsConnectedToExpander) {
        if (tReturnValue == 0) {
            I2CInit();          // init only once
            PCA9685Reset();     // reset only once
        }
        PCA9685Init(); // initialize at every attach is simpler but initializing once for every board would be sufficient.
        return tReturnValue;
    }
#  else
    if (tReturnValue == 0) {
        I2CInit();          // init only once
        PCA9685Reset();     // reset only once
    }
    PCA9685Init(); // initialize at every attach is simpler but initializing once for every board would be sufficient.
    return tReturnValue;
#  endif
#elif defined(USE_LEIGHTWEIGHT_SERVO_LIB)
    if(aPin != 9 && aPin != 10) {
        return false;
    }
    return aPin;
#endif // defined(USE_PCA9685_SERVO_EXPANDER)

#if !defined(_DO_NOT_USE_SERVO_LIB)
    // This error value has priority over the regular return value from Servo::attach()
    if (tReturnValue == INVALID_SERVO) {
        return tReturnValue;
    }
    mCurrentMicrosecondsOrUnits = DEFAULT_PULSE_WIDTH;
    /*
     * Call attach() of the underlying Servo library
     */
#  if defined(ARDUINO_ARCH_APOLLO3)
    Servo::attach(aPin, tMicrosecondsForServo0Degree, tMicrosecondsForServo180Degree);
    return aPin; // Sparkfun apollo3 Servo library has no return value for attach :-(
#  else
    return Servo::attach(aPin, tMicrosecondsForServo0Degree, tMicrosecondsForServo180Degree);
#  endif
#endif //!defined(_DO_NOT_USE_SERVO_LIB)
}

void ServoEasing::detach() {
    if (mServoIndex != INVALID_SERVO) {
        ServoEasingArray[mServoIndex] = NULL;
        // If servo with highest index in array was detached, we want to find new sServoArrayMaxIndex
        while (ServoEasingArray[sServoArrayMaxIndex] == NULL && sServoArrayMaxIndex > 0) {
            sServoArrayMaxIndex--;
        }

#if defined(USE_PCA9685_SERVO_EXPANDER)
#  if defined(USE_SERVO_LIB)
        if (mServoIsConnectedToExpander) {
            setPWM(0); // set signal fully off
        } else {
            Servo::detach();
        }
#  else
        setPWM(0); // set signal fully off
#  endif
#elif defined(USE_LEIGHTWEIGHT_SERVO_LIB)
        deinitLightweightServoPin9_10(mServoPin == 9, mServoPin == 10); // disable output and change to input
#else
        Servo::detach();
#endif
    }
    mServoMoves = false; // safety net to enable right update handling if accidentally called
    mServoIndex = INVALID_SERVO;
}

/**
 * @note Reverse means, that values for 180 and 0 degrees are swapped by: aValue = mServo180DegreeMicrosecondsOrUnits - (aValue - mServo0DegreeMicrosecondsOrUnits)
 * Be careful, if you specify different end values, it may not behave, as you expect.
 * For this case better use the attach function with 5 parameter.
 * This flag is only used at writeMicrosecondsOrUnits()
 */
void ServoEasing::setReverseOperation(bool aOperateServoReverse) {
    mOperateServoReverse = aOperateServoReverse;
}

uint_fast16_t ServoEasing::getSpeed() {
    return mSpeed;
}

void ServoEasing::setSpeed(uint_fast16_t aDegreesPerSecond) {
    mSpeed = aDegreesPerSecond;
}

/**
 * @param aTrimDegrees This trim value is always added to the degree/units/microseconds value requested
 * @param aDoWrite Apply value directly to servo by calling writeMicrosecondsOrUnits()
 */
void ServoEasing::setTrim(int aTrimDegrees, bool aDoWrite) {
    if (aTrimDegrees >= 0) {
        setTrimMicrosecondsOrUnits(DegreeToMicrosecondsOrUnits(aTrimDegrees) - mServo0DegreeMicrosecondsOrUnits, aDoWrite);
    } else {
        setTrimMicrosecondsOrUnits(-(DegreeToMicrosecondsOrUnits(-aTrimDegrees) - mServo0DegreeMicrosecondsOrUnits), aDoWrite);
    }
}

/**
 * @param aTrimMicrosecondsOrUnits This trim value is always added to the degree/units/microseconds value requested
 * @param aDoWrite Apply value directly to servo by calling writeMicrosecondsOrUnits()
 * @note It is only used/added at writeMicrosecondsOrUnits()
 */
void ServoEasing::setTrimMicrosecondsOrUnits(int aTrimMicrosecondsOrUnits, bool aDoWrite) {
    mTrimMicrosecondsOrUnits = aTrimMicrosecondsOrUnits;
    if (aDoWrite) {
        writeMicrosecondsOrUnits(mCurrentMicrosecondsOrUnits);
    }
}

#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
void ServoEasing::setEasingType(uint_fast8_t aEasingType) {
    mEasingType = aEasingType;
}

uint_fast8_t ServoEasing::getEasingType() {
    return (mEasingType);
}

void ServoEasing::registerUserEaseInFunction(float (*aUserEaseInFunction)(float aFactorOfTimeCompletion)) {
    mUserEaseInFunction = aUserEaseInFunction;
}
#endif

/*
 * @param aValue treat values less than 400 as angles in degrees, others are handled as microseconds
 */
void ServoEasing::write(int aDegreeOrMicrosecond) {
#if defined(TRACE)
    Serial.print(F("write "));
    Serial.print(aDegreeOrMicrosecond);
    Serial.print(' ');
#endif
    /*
     * Check for valid initialization of servo.
     */
    if (mServoIndex == INVALID_SERVO) {
#if defined(TRACE)
        Serial.print(F("Error: detached servo"));
#endif
        return;
    }
    ServoEasingNextPositionArray[mServoIndex] = aDegreeOrMicrosecond;
    writeMicrosecondsOrUnits(DegreeToMicrosecondsOrUnits(aDegreeOrMicrosecond));
}

/**
 * Before sending the value to the underlying Servo library, trim and reverse is applied
 */
void ServoEasing::writeMicrosecondsOrUnits(int aMicrosecondsOrUnits) {
    /*
     * Check for valid initialization of servo.
     */
    if (mServoIndex == INVALID_SERVO) {
#if defined(TRACE)
        Serial.print(F("Error: detached servo"));
#endif
        return;
    }

    mCurrentMicrosecondsOrUnits = aMicrosecondsOrUnits;

#if defined(TRACE)
    Serial.print(mServoIndex);
    Serial.print('/');
    Serial.print(mServoPin);
    Serial.print(F(" us/u="));
    Serial.print(aMicrosecondsOrUnits);
    if (mTrimMicrosecondsOrUnits != 0) {
        Serial.print(" t=");
        Serial.print(aMicrosecondsOrUnits + mTrimMicrosecondsOrUnits);
    }
#endif // TRACE

// Apply trim - this is the only place mTrimMicrosecondsOrUnits is evaluated
    aMicrosecondsOrUnits += mTrimMicrosecondsOrUnits;
// Apply reverse, values for 0 to 180 are swapped if reverse - this is the only place mOperateServoReverse is evaluated
// (except in the DegreeToMicrosecondsOrUnitsWithTrimAndReverse() function for external testing purposes)
    if (mOperateServoReverse) {
        aMicrosecondsOrUnits = mServo180DegreeMicrosecondsOrUnits - (aMicrosecondsOrUnits - mServo0DegreeMicrosecondsOrUnits);
#if defined(TRACE)
        Serial.print(F(" r="));
        Serial.print(aMicrosecondsOrUnits);
#endif
    }

#if defined(PRINT_FOR_SERIAL_PLOTTER)
    Serial.print(' ');
    Serial.print(aMicrosecondsOrUnits);
#endif

#if defined(USE_LEIGHTWEIGHT_SERVO_LIB)
    writeMicrosecondsLightweightServo(aMicrosecondsOrUnits, (mServoPin == 9));

#elif defined(USE_PCA9685_SERVO_EXPANDER)
#  if defined(TRACE)
    // For each pin show PWM on value used below
    Serial.print(F(" s="));
    Serial.print(mServoPin * (4096 - (DEFAULT_PCA9685_UNITS_FOR_180_DEGREE + 100)) / 15); // mServoPin * 233
#  endif
#  if defined(USE_SERVO_LIB)
    if (mServoIsConnectedToExpander) {
        setPWM(mServoPin * ((4096 - (DEFAULT_PCA9685_UNITS_FOR_180_DEGREE + 100)) / 15), aMicrosecondsOrUnits); // mServoPin * 233
    } else {
        Servo::writeMicroseconds(aMicrosecondsOrUnits); // requires 7 �s
    }
#  else
    /*
     * Distribute the servo start time over the 20 ms period.
     * Unexpectedly this even saves 20 bytes Flash for an ATmega328P
     */
    setPWM(mServoPin * ((4096 - (DEFAULT_PCA9685_UNITS_FOR_180_DEGREE + 100)) / 15), aMicrosecondsOrUnits); // mServoPin * 233
#  endif

#else
    Servo::writeMicroseconds(aMicrosecondsOrUnits); // requires 7 �s
#endif

#if defined(TRACE)
    Serial.println();
#endif
}

/**
 * Only used in startEaseTo to compute target degree
 * For PCA9685, we have stored units in mServo0DegreeMicrosecondsOrUnits and mServo180DegreeMicrosecondsOrUnits
 * @param aMicroseconds Always assume microseconds, thus for PCA9685 we must convert 0 and 180 degree values back to microseconds
 */
int ServoEasing::MicrosecondsToDegree(int aMicroseconds) {
#if defined(USE_PCA9685_SERVO_EXPANDER)
#  if defined(USE_SERVO_LIB)
    if (!mServoIsConnectedToExpander) {
        return MicrosecondsOrUnitsToDegree(aMicroseconds); // not connected to PCA9685 here
    }
#  endif
    int32_t tResult = aMicroseconds - PCA9685UnitsToMicroseconds(mServo0DegreeMicrosecondsOrUnits);
    tResult = (tResult * 180) + 928;
    return (tResult / PCA9685UnitsToMicroseconds((mServo180DegreeMicrosecondsOrUnits - mServo0DegreeMicrosecondsOrUnits)));
#else
    return MicrosecondsOrUnitsToDegree(aMicroseconds);
#endif
}

/**
 * Used to convert e.g. mCurrentMicrosecondsOrUnits back to degree
 * @param aMicrosecondsOrUnits For servos connected to a PCA9685 assume units, else assume microseconds
 * Do not use map function, because it does no rounding
 */
int ServoEasing::MicrosecondsOrUnitsToDegree(int aMicrosecondsOrUnits) {
    /*
     * Formula for microseconds:
     * (aMicrosecondsOrUnits - mServo0DegreeMicrosecondsOrUnits) * (180 / 1856) // 1856 = 180 - 0 degree micros
     * Formula for PCA9685 units
     * (aMicrosecondsOrUnits - mServo0DegreeMicrosecondsOrUnits) * (180 / 380) // 380 = 180 - 0 degree units
     * Formula for both without rounding
     * map(aMicrosecondsOrUnits, mServo0DegreeMicrosecondsOrUnits, mServo180DegreeMicrosecondsOrUnits, 0, 180)
     */

    /*
     * compute map with rounding
     */
    // remove zero degree offset
    int32_t tResult = aMicrosecondsOrUnits - mServo0DegreeMicrosecondsOrUnits;
#if defined(USE_PCA9685_SERVO_EXPANDER)
#  if defined(USE_SERVO_LIB)
    if (mServoIsConnectedToExpander) {
        // here we have PCA9685 units
        tResult = (tResult * 180) + 190;
    } else {
        // here we deal with microseconds
        tResult = (tResult * 180) + 928;
    }
#  else
    // here we have PCA9685 units
    tResult = (tResult * 180) + 190;
#  endif
#else
    // here we deal with microseconds
    tResult = (tResult * 180) + 928; // 928 is the value for 1/2 degree before scaling; (1856 = 180 - 0 degree micros) / 2
#endif
    // scale by 180 degree range (180 - 0 degree micros)
    return (tResult / (mServo180DegreeMicrosecondsOrUnits - mServo0DegreeMicrosecondsOrUnits));

}

int ServoEasing::MicrosecondsOrUnitsToMicroseconds(int aMicrosecondsOrUnits) {
#if defined(USE_PCA9685_SERVO_EXPANDER)
    return PCA9685UnitsToMicroseconds(aMicrosecondsOrUnits);
#else
    return aMicrosecondsOrUnits; // we have microseconds here
#endif

}
/**
 * We have around 10 �s per degree
 * We do not convert values >= 400.
 * Used to convert (external) provided degree values to internal microseconds
 */
int ServoEasing::DegreeToMicrosecondsOrUnits(int aDegreeOrMicroseconds) {
// For microseconds and PCA9685 units:
#if defined(DISABLE_MICROS_AS_DEGREE_PARAMETER)
    return map(aDegreeOrMicroseconds, 0, 180, mServo0DegreeMicrosecondsOrUnits, mServo180DegreeMicrosecondsOrUnits);
#else
    if (aDegreeOrMicroseconds < THRESHOLD_VALUE_FOR_INTERPRETING_VALUE_AS_MICROSECONDS) {
        return map(aDegreeOrMicroseconds, 0, 180, mServo0DegreeMicrosecondsOrUnits, mServo180DegreeMicrosecondsOrUnits);
    } else {
        // here aDegreeOrMicroseconds contains microseconds
#  if defined(USE_PCA9685_SERVO_EXPANDER)
#    if defined(USE_SERVO_LIB)
    if (!mServoIsConnectedToExpander) {
        return aDegreeOrMicroseconds; // we must return microseconds here
    }
#    endif
    return MicrosecondsToPCA9685Units(aDegreeOrMicroseconds); // return units here
#  else
        return aDegreeOrMicroseconds;
#  endif
    }
#endif // defined(DISABLE_MICROS_AS_DEGREE_PARAMETER)
}

/**
 * Mainly for testing, since trim and reverse are applied at each write.
 */
int ServoEasing::DegreeToMicrosecondsOrUnitsWithTrimAndReverse(int aDegree) {
// For microseconds and PCA9685 units:
    int tResultValue = map(aDegree, 0, 180, mServo0DegreeMicrosecondsOrUnits, mServo180DegreeMicrosecondsOrUnits);
    tResultValue += mTrimMicrosecondsOrUnits;
    if (mOperateServoReverse) {
        tResultValue = mServo180DegreeMicrosecondsOrUnits - (tResultValue - mServo0DegreeMicrosecondsOrUnits);
    }
    return tResultValue;
}

void ServoEasing::easeTo(int aDegreeOrMicrosecond) {
    easeTo(aDegreeOrMicrosecond, mSpeed);
}

/**
 * Blocking move without interrupt
 * @param aDegreesPerSecond Can range from 1 to the physically maximum value of 450
 */
void ServoEasing::easeTo(int aDegreeOrMicrosecond, uint_fast16_t aDegreesPerSecond) {
    startEaseTo(aDegreeOrMicrosecond, aDegreesPerSecond, DO_NOT_START_UPDATE_BY_INTERRUPT); // no interrupts
    do {
        // First do the delay, then check for update, since we are likely called directly after start and there is nothing to move yet
        delay(REFRESH_INTERVAL_MILLIS); // 20 ms
#if defined(PRINT_FOR_SERIAL_PLOTTER)
    } while (!updateAllServos());
#else
    } while (!update());
#endif
}

void ServoEasing::easeToD(int aDegreeOrMicrosecond, uint_fast16_t aMillisForMove) {
    startEaseToD(aDegreeOrMicrosecond, aMillisForMove, DO_NOT_START_UPDATE_BY_INTERRUPT);
    do {
        delay(REFRESH_INTERVAL_MILLIS); // 20 ms
#if defined(PRINT_FOR_SERIAL_PLOTTER)
    } while (!updateAllServos());
#else
    } while (!update());
#endif
}

bool ServoEasing::setEaseTo(int aDegreeOrMicrosecond) {
    return startEaseTo(aDegreeOrMicrosecond, mSpeed, DO_NOT_START_UPDATE_BY_INTERRUPT);
}

/**
 * Sets easing parameter, but does not start interrupt
 * @return false if servo was still moving
 */
bool ServoEasing::setEaseTo(int aDegreeOrMicrosecond, uint_fast16_t aDegreesPerSecond) {
    return startEaseTo(aDegreeOrMicrosecond, aDegreesPerSecond, DO_NOT_START_UPDATE_BY_INTERRUPT);
}

/**
 * Starts interrupt for update()
 */
bool ServoEasing::startEaseTo(int aDegreeOrMicrosecond) {
    return startEaseTo(aDegreeOrMicrosecond, mSpeed, START_UPDATE_BY_INTERRUPT);
}

/**
 * Compute the MillisForCompleteMove parameter for use of startEaseToD() function
 * and handle CALL_STYLE_BOUNCING_OUT_IN flag, which requires double time
 * @return false if servo was still moving
 */
bool ServoEasing::startEaseTo(int aDegreeOrMicrosecond, uint_fast16_t aDegreesPerSecond, bool aStartUpdateByInterrupt) {

    /*
     * Avoid division by 0 below
     */
    if (aDegreesPerSecond == 0) {
#if defined(DEBUG)
        Serial.println(F("Speed is 0 -> set to 1"));
#endif
        aDegreesPerSecond = 1;
    }

    /*
     * Get / convert target degree
     */
    int tTargetDegree = aDegreeOrMicrosecond;
#if defined(DISABLE_MICROS_AS_DEGREE_PARAMETER)
     tTargetDegree = aDegreeOrMicrosecond; // no conversion required here
#else
    // Convert aDegreeOrMicrosecond to target degree
    if (aDegreeOrMicrosecond >= THRESHOLD_VALUE_FOR_INTERPRETING_VALUE_AS_MICROSECONDS) {
        tTargetDegree = MicrosecondsToDegree(aDegreeOrMicrosecond);
    }
#endif

    int tCurrentDegree = MicrosecondsOrUnitsToDegree(mCurrentMicrosecondsOrUnits);

    /*
     * Compute the MillisForCompleteMove parameter for use of startEaseToD() function
     */
    uint_fast16_t tMillisForCompleteMove = abs(tTargetDegree - tCurrentDegree) * MILLIS_IN_ONE_SECOND / aDegreesPerSecond;

    // bouncing has double movement, so take double time
#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
    if ((mEasingType & CALL_STYLE_MASK) == CALL_STYLE_BOUNCING_OUT_IN) {
        tMillisForCompleteMove *= 2;
    }
#endif

    return startEaseToD(aDegreeOrMicrosecond, tMillisForCompleteMove, aStartUpdateByInterrupt);
}

/**
 * Sets easing parameter, but does not start
 * @return false if servo was still moving
 */
bool ServoEasing::setEaseToD(int aDegree, uint_fast16_t aMillisForMove) {
    return startEaseToD(aDegree, aMillisForMove, DO_NOT_START_UPDATE_BY_INTERRUPT);
}

/*
 * stay at the position for aMillisToWait
 * Used as delay for callback
 */
bool ServoEasing::noMovement(uint_fast16_t aMillisToWait) {
    return startEaseToD(MicrosecondsOrUnitsToMicroseconds(mCurrentMicrosecondsOrUnits), aMillisToWait, START_UPDATE_BY_INTERRUPT);
}

/**
 * Sets up all the values required for a smooth move to new value
 * Lower level function with time instead of speed parameter
 * @return false if servo was still moving
 */
bool ServoEasing::startEaseToD(int aDegreeOrMicrosecond, uint_fast16_t aMillisForMove, bool aStartUpdateByInterrupt) {
    /*
     * Check for valid initialization of servo.
     */
    if (mServoIndex == INVALID_SERVO) {
#if defined(TRACE)
        Serial.print(F("Error: detached servo"));
#endif
        return true;
    }

#if defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
    if (true) {
#else
    if (mEasingType != EASE_DUMMY_MOVE) {
#endif
        // write the position also to ServoEasingNextPositionArray
        ServoEasingNextPositionArray[mServoIndex] = aDegreeOrMicrosecond;
        // No end position for dummy move. This forces mDeltaMicrosecondsOrUnits to zero, avoiding any movement
        mEndMicrosecondsOrUnits = DegreeToMicrosecondsOrUnits(aDegreeOrMicrosecond);
    }
    int tCurrentMicrosecondsOrUnits = mCurrentMicrosecondsOrUnits;
    mDeltaMicrosecondsOrUnits = mEndMicrosecondsOrUnits - tCurrentMicrosecondsOrUnits;

    mMillisForCompleteMove = aMillisForMove;
    mStartMicrosecondsOrUnits = tCurrentMicrosecondsOrUnits;

#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
    if ((mEasingType & CALL_STYLE_MASK) == CALL_STYLE_BOUNCING_OUT_IN) {
        // bouncing has same end position as start position
        mEndMicrosecondsOrUnits = tCurrentMicrosecondsOrUnits;
    }
#endif

    mMillisAtStartMove = millis();

#if defined(TRACE)
printDynamic(&Serial, true);
#elif defined(DEBUG)
printDynamic(&Serial);
#endif

    bool tReturnValue = !mServoMoves;

// Check after printDynamic() to see the values
    mServoMoves = true;
    if (aStartUpdateByInterrupt && !sInterruptsAreActive) {
        enableServoEasingInterrupt();
    }

    return tReturnValue;
}

void ServoEasing::stop() {
    mServoMoves = false;
    if (!isOneServoMoving()) {
        // disable interrupt only if all servos stopped. This enables independent movements of servos with one interrupt handler.
        disableServoEasingInterrupt();
    }
}

void ServoEasing::continueWithInterrupts() {
    mServoMoves = true;
    enableServoEasingInterrupt();
}

void ServoEasing::continueWithoutInterrupts() {
    mServoMoves = true;
}

void ServoEasing::setTargetPositionReachedHandler(void (*aTargetPositionReachedHandler)(ServoEasing*)) {
    TargetPositionReachedHandler = aTargetPositionReachedHandler;
}

/*
 * returns true if endAngle was reached / servo stopped
 */
#if defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
bool ServoEasing::update() {

    if (!mServoMoves) {
        return true;
    }

    uint32_t tMillisSinceStart = millis() - mMillisAtStartMove;
    if (tMillisSinceStart >= mMillisForCompleteMove) {
        // end of time reached -> write end position and return true
        writeMicrosecondsOrUnits(mEndMicrosecondsOrUnits);
        mServoMoves = false;
        if(TargetPositionReachedHandler != NULL){
            // Call end callback function
            TargetPositionReachedHandler(this);
        }
        return !mServoMoves; // mServoMoves may be changed by callback handler
    }
    /*
     * Use faster non float arithmetic
     * Linear movement: new position is: start position + total delta * (millis_done / millis_total aka "percentage of completion")
     * 40 �s to compute
     */
    int_fast16_t tNewMicrosecondsOrUnits = mStartMicrosecondsOrUnits
    + ((mDeltaMicrosecondsOrUnits * (int32_t) tMillisSinceStart) / mMillisForCompleteMove);
    /*
     * Write new position only if changed
     */
    if (tNewMicrosecondsOrUnits != mCurrentMicrosecondsOrUnits) {
        writeMicrosecondsOrUnits(tNewMicrosecondsOrUnits);
    }
    return false;
}

#else // PROVIDE_ONLY_LINEAR_MOVEMENT
bool ServoEasing::update() {

    if (!mServoMoves) {
#  if defined(PRINT_FOR_SERIAL_PLOTTER)
        // call it always for serial plotter
        writeMicrosecondsOrUnits(mCurrentMicrosecondsOrUnits);
#  endif
        return true;
    }

    uint32_t tMillisSinceStart = millis() - mMillisAtStartMove;
    if (tMillisSinceStart >= mMillisForCompleteMove) {
        // end of time reached -> write end position and return true
        writeMicrosecondsOrUnits(mEndMicrosecondsOrUnits);
        mServoMoves = false;
        if (TargetPositionReachedHandler != NULL) {
            // Call end callback function
            TargetPositionReachedHandler(this);
        }
        return !mServoMoves; // mServoMoves may be changed by callback handler
    }

    int tNewMicrosecondsOrUnits;
    if (mEasingType == EASE_LINEAR) {
        /*
         * Use faster non float arithmetic
         * Linear movement: new position is: start position + total delta * (millis_done / millis_total aka "percentage of completion")
         * 40 �s to compute
         * Cast to int32 required for mMillisForCompleteMove for 32 bit platforms, otherwise we divide signed by unsigned. Thanks to drifkind.
         */
        tNewMicrosecondsOrUnits = mStartMicrosecondsOrUnits
                + ((mDeltaMicrosecondsOrUnits * (int32_t) tMillisSinceStart) / (int32_t) mMillisForCompleteMove);
    } else {
        /*
         * Non linear movement -> use floats
         * Compute tPercentageOfCompletion - from 0.0 to 1.0
         * The expected result of easing function is from 0.0 to 1.0
         * or from EASE_FUNCTION_DEGREE_INDICATOR_OFFSET to EASE_FUNCTION_DEGREE_INDICATOR_OFFSET + 180 for direct degree result
         */
        float tFactorOfTimeCompletion = (float) tMillisSinceStart / (float) mMillisForCompleteMove;
        float tFactorOfMovementCompletion = 0.0;

        uint_fast8_t tCallStyle = mEasingType & CALL_STYLE_MASK; // Values are CALL_STYLE_DIRECT, CALL_STYLE_OUT, CALL_STYLE_IN_OUT, CALL_STYLE_BOUNCING_OUT_IN

        if (tCallStyle == CALL_STYLE_DIRECT) {
            // Use IN function direct: Call with PercentageOfCompletion | 0.0 to 1.0. Result is from 0.0 to 1.0
            tFactorOfMovementCompletion = callEasingFunction(tFactorOfTimeCompletion);

        } else if (tCallStyle == CALL_STYLE_OUT) {
            // Use IN function to generate OUT function: Call with (1 - PercentageOfCompletion) | 1.0 to 0.0. Result = (1 - result)
            tFactorOfMovementCompletion = 1.0 - (callEasingFunction(1.0 - tFactorOfTimeCompletion));

        } else {
            if (tFactorOfTimeCompletion <= 0.5) {
                if (tCallStyle == CALL_STYLE_IN_OUT) {
                    // In the first half, call with (2 * PercentageOfCompletion) | 0.0 to 1.0. Result = (0.5 * result)
                    tFactorOfMovementCompletion = 0.5 * (callEasingFunction(2.0 * tFactorOfTimeCompletion));
                }
                if (tCallStyle == CALL_STYLE_BOUNCING_OUT_IN) {
                    // In the first half, call with (1 - (2 * PercentageOfCompletion)) | 1.0 to 0.0. Result = (1 - result) -> call OUT function faster.
                    tFactorOfMovementCompletion = 1.0 - (callEasingFunction(1.0 - (2.0 * tFactorOfTimeCompletion)));
                }
            } else {
                if (tCallStyle == CALL_STYLE_IN_OUT) {
                    // In the second half, call with (2 - (2 * PercentageOfCompletion)) | 1.0 to 0.0. Result = ( 1- (0.5 * result))
                    tFactorOfMovementCompletion = 1.0 - (0.5 * (callEasingFunction(2.0 - (2.0 * tFactorOfTimeCompletion))));
                }
                if (tCallStyle == CALL_STYLE_BOUNCING_OUT_IN) {
                    // In the second half, call with ((2 * PercentageOfCompletion) - 1) | 0.0 to 1.0. Result = (1- result) -> call OUT function faster and backwards.
                    tFactorOfMovementCompletion = 1.0 - (callEasingFunction((2.0 * tFactorOfTimeCompletion) - 1.0));
                }
            }
        }

        if (tFactorOfMovementCompletion >= EASE_FUNCTION_DEGREE_INDICATOR_OFFSET / 2) {
            // Here we have called an easing function, which returns degree instead of the factor of completion (0.0 to 1.0)
            tNewMicrosecondsOrUnits = DegreeToMicrosecondsOrUnits(
                    tFactorOfMovementCompletion - EASE_FUNCTION_DEGREE_INDICATOR_OFFSET + 0.5);
        } else {
            int tDeltaMicroseconds = mDeltaMicrosecondsOrUnits * tFactorOfMovementCompletion; // having this as int value saves float operations
            tNewMicrosecondsOrUnits = mStartMicrosecondsOrUnits + tDeltaMicroseconds;
        }
    }

#  if defined(PRINT_FOR_SERIAL_PLOTTER)
    // call it always for serial plotter
    writeMicrosecondsOrUnits(tNewMicrosecondsOrUnits);
#  else
    /*
     * Write new position only if changed
     */
    if (tNewMicrosecondsOrUnits != mCurrentMicrosecondsOrUnits) {
        writeMicrosecondsOrUnits(tNewMicrosecondsOrUnits);
    }
#  endif
    return false;
}

float ServoEasing::callEasingFunction(float aFactorOfTimeCompletion) {
    uint_fast8_t tEasingType = mEasingType & EASE_TYPE_MASK;

    switch (tEasingType) {

    case EASE_USER_DIRECT:
        if (mUserEaseInFunction != NULL) {
            return mUserEaseInFunction(aFactorOfTimeCompletion);
        } else {
            return 0.0;
        }

    case EASE_QUADRATIC_IN:
        return QuadraticEaseIn(aFactorOfTimeCompletion);
    case EASE_CUBIC_IN:
        return CubicEaseIn(aFactorOfTimeCompletion);
    case EASE_QUARTIC_IN:
        return QuarticEaseIn(aFactorOfTimeCompletion);
#  if !defined(DISABLE_COMPLEX_FUNCTIONS)
    case EASE_SINE_IN:
        return SineEaseIn(aFactorOfTimeCompletion);
    case EASE_CIRCULAR_IN:
        return CircularEaseIn(aFactorOfTimeCompletion);
    case EASE_BACK_IN:
        return BackEaseIn(aFactorOfTimeCompletion);
    case EASE_ELASTIC_IN:
        return ElasticEaseIn(aFactorOfTimeCompletion);
    case EASE_BOUNCE_OUT:
        return EaseOutBounce(aFactorOfTimeCompletion);
#  endif
    default:
        return 0.0;
    }
}

#endif //PROVIDE_ONLY_LINEAR_MOVEMENT

bool ServoEasing::isMoving() {
#if defined(ESP8266)
    yield(); // Not required for ESP32, since our code is running on CPU1 and using yield seems to disturb the I2C interface
#endif
    return mServoMoves;
}

/*
 * Call yield here (actually only for ESP8266), so the user do not need to care for it in long running loops.
 * yield() will only allow higher priority tasks to run.
 * This call is dangerous for ESP32, since the timer is detached AFTER mServoMoves is set to false in handleServoTimerInterrupt(),
 * and one core may check mServoMoves and started a new move with initializing the timer, and then the timer gets detached by handleServoTimerInterrupt()
 * which leads to an error: CORRUPT HEAP: Bad head at ...
 */
bool ServoEasing::isMovingAndCallYield() {
#if defined(ESP8266)
    yield(); // Not required for ESP32, since our code is running on CPU1 and using yield seems to disturb the I2C interface
#endif
    return mServoMoves;
}

int ServoEasing::getCurrentAngle() {
    return MicrosecondsOrUnitsToDegree(mCurrentMicrosecondsOrUnits);
}

int ServoEasing::getEndMicrosecondsOrUnits() {
    return mEndMicrosecondsOrUnits;
}

/*
 * Not used internally
 */
int ServoEasing::getEndMicrosecondsOrUnitsWithTrim() {
    return mEndMicrosecondsOrUnits + mTrimMicrosecondsOrUnits;
}

int ServoEasing::getDeltaMicrosecondsOrUnits() {
    return mDeltaMicrosecondsOrUnits;
}

int ServoEasing::getMillisForCompleteMove() {
    return mMillisForCompleteMove;
}

/**
 * Do a printDynamic() and a printStatic()
 * @param aSerial The Print object on which to write, for Arduino you can use &Serial.
 * @param doExtendedOutput Print also microseconds values for degrees.
 */
void ServoEasing::print(Print *aSerial, bool doExtendedOutput) {
    printDynamic(aSerial, doExtendedOutput);
    printStatic(aSerial);
}

/**
 * @param aEasingType No range checking!
 */
void ServoEasing::printEasingType(Print *aSerial, uint_fast8_t aEasingType) {
#  if defined(__AVR__)
    const char *tEaseTypeStringPtr = (char*) pgm_read_word(&easeTypeStrings[aEasingType & EASE_TYPE_MASK]);
    aSerial->print((__FlashStringHelper*) (tEaseTypeStringPtr));
#  else
   aSerial->print(easeTypeStrings[aEasingType]);
#  endif
    uint_fast8_t tEasingTypeCallStyle = aEasingType & CALL_STYLE_MASK;
    if (tEasingTypeCallStyle == CALL_STYLE_IN) {
        aSerial->print(F("_in"));
    } else if (tEasingTypeCallStyle == CALL_STYLE_OUT) {
        aSerial->print(F("_out"));
    } else if (tEasingTypeCallStyle == CALL_STYLE_IN_OUT) {
        aSerial->print(F("_in_out"));
    } else {
        aSerial->print(F("_bouncing_in_out"));
    }
}

/**
 * Prints values which may change from move to move.
 * @param aSerial The Print object on which to write, for Arduino you can use &Serial.
 * @param doExtendedOutput Print also microseconds values for degrees.
 */
void ServoEasing::printDynamic(Print *aSerial, bool doExtendedOutput) {
// pin is static but it is required for identifying the servo
    aSerial->print(mServoIndex);
    aSerial->print('/');
    aSerial->print(mServoPin);
    aSerial->print(F(": "));

    aSerial->print(MicrosecondsOrUnitsToDegree(mCurrentMicrosecondsOrUnits));
    if (doExtendedOutput) {
        aSerial->print('|');
        aSerial->print(mCurrentMicrosecondsOrUnits);
    }

    aSerial->print(F(" -> "));
    aSerial->print(MicrosecondsOrUnitsToDegree(mEndMicrosecondsOrUnits));
    if (doExtendedOutput) {
        aSerial->print('|');
        aSerial->print(mEndMicrosecondsOrUnits);
    }

    aSerial->print(F(" = "));
    int tDelta;
    if (mDeltaMicrosecondsOrUnits >= 0) {
        tDelta = MicrosecondsOrUnitsToDegree(mDeltaMicrosecondsOrUnits + mServo0DegreeMicrosecondsOrUnits);
    } else {
        tDelta = -MicrosecondsOrUnitsToDegree(mServo0DegreeMicrosecondsOrUnits - mDeltaMicrosecondsOrUnits);
    }
    aSerial->print(tDelta);
    if (doExtendedOutput) {
        aSerial->print('|');
        aSerial->print(mDeltaMicrosecondsOrUnits);
    }

    aSerial->print(F(" in "));
    aSerial->print(mMillisForCompleteMove);
    aSerial->print(F(" ms"));

    aSerial->print(F(" with speed="));
    aSerial->print(mSpeed);

#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
    aSerial->print(F(" and easingType=0x"));
    aSerial->print(mEasingType, HEX);
    aSerial->print('|');
    printEasingType(aSerial, mEasingType);
#endif

    if (doExtendedOutput) {
        aSerial->print(F(" MillisAtStartMove="));
        aSerial->print(mMillisAtStartMove);
    }

    aSerial->println();
}

/*
 * Prints values which normally does NOT change from move to move.
 * @param aSerial The Print object on which to write, for Arduino you can use &Serial.
 */
void ServoEasing::printStatic(Print *aSerial) {

    aSerial->print(F("0="));
    aSerial->print(mServo0DegreeMicrosecondsOrUnits);
    aSerial->print(F(" 180="));
    aSerial->print(mServo180DegreeMicrosecondsOrUnits);

    aSerial->print(F(" trim="));
    if (mTrimMicrosecondsOrUnits >= 0) {
        aSerial->print(MicrosecondsOrUnitsToDegree(mTrimMicrosecondsOrUnits + mServo0DegreeMicrosecondsOrUnits));
    } else {
        aSerial->print(-MicrosecondsOrUnitsToDegree(mServo0DegreeMicrosecondsOrUnits - mTrimMicrosecondsOrUnits));
    }
    aSerial->print('|');
    aSerial->print(mTrimMicrosecondsOrUnits);

    aSerial->print(F(" reverse="));
    aSerial->print(mOperateServoReverse);

#if defined(USE_PCA9685_SERVO_EXPANDER)
    aSerial->print(F(" PCA9685I2CAddress=0x"));
    aSerial->print(mPCA9685I2CAddress, HEX);
#if !defined(USE_SOFT_I2C_MASTER)
    aSerial->print(F(" &Wire=0x"));
//    aSerial->print((uintptr_t) mI2CClass, HEX); // defined since C++11
    aSerial->print((uint_fast16_t) mI2CClass, HEX);
#endif

#  if defined(USE_SERVO_LIB)
    aSerial->print(F(" at expander="));
    aSerial->print(mServoIsConnectedToExpander);
#  endif
#endif

    aSerial->print(F(" callback=0x"));
    aSerial->print((__SIZE_TYPE__) TargetPositionReachedHandler, HEX);

    aSerial->print(F(" MAX_EASING_SERVOS="));
    aSerial->print(MAX_EASING_SERVOS);

    aSerial->print(F(" this=0x"));
    aSerial->println((uint_fast16_t) this, HEX);
}

/*
 * Clips the unsigned degree value and handles unsigned underflow.
 * returns 0 if aDegreeToClip >= 218
 * returns 180 if 180 <= aDegreeToClip < 218
 */
int clipDegreeSpecial(uint_fast8_t aDegreeToClip) {
    if (aDegreeToClip) {
        return aDegreeToClip;
    }
    if (aDegreeToClip < 218) {
        return 180;
    }
    return 0;
}

/*
 * The recommended test if at least one servo is moving yet.
 */
bool ServoEasing::areInterruptsActive() {
#if defined(ESP8266)
    yield(); // required for ESP8266
#endif
    return sInterruptsAreActive;
}

/*
 * Update all servos from list and check if all servos have stopped.
 * Defined weak in order to be able to overwrite it, e.g. for synchronizing with NeoPixel updates,
 * which otherwise leads to servo jitter. See QuadrupedNeoPixel.cpp of QuadrupedControl example.
 * We have 100 us before the next servo period starts.
 */
#if defined(STM32F1xx) && STM32_CORE_VERSION_MAJOR == 1 &&  STM32_CORE_VERSION_MINOR <= 8 // for "Generic STM32F1 series" from STM32 Boards from STM32 cores of Arduino Board manager
__attribute__((weak)) void handleServoTimerInterrupt(HardwareTimer *aDummy __attribute__((unused))) // changed in stm32duino 1.9 - 5/2020
#else
__attribute__((weak)) void handleServoTimerInterrupt()
#endif
{
#if defined(USE_PCA9685_SERVO_EXPANDER)
// Otherwise it will hang forever in I2C transfer
#  if !defined(ARDUINO_ARCH_MBED)
    interrupts();
#  endif
#endif
    if (updateAllServos()) {
        // disable interrupt only if all servos stopped. This enables independent movements of servos with this interrupt handler.
        disableServoEasingInterrupt();
    }
}

// The eclipse formatter has problems with // comments in undefined code blocks
// !!! Must be without comment and closed by @formatter:on
// @formatter:off
/*
 * Timer1 is used for the Arduino Servo library.
 * To have non blocking easing functions its unused channel B is used to generate an interrupt 100 �s before the end of the 20 ms Arduino Servo refresh period.
 * This interrupt then updates all servo values for the next refresh period.
 * First interrupt is triggered not directly, but after 20 ms, since we are often called here at the time of the last interrupt of the preceding servo move.
 */
void enableServoEasingInterrupt() {
#if defined(__AVR__)
#  if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
#    if defined(USE_PCA9685_SERVO_EXPANDER) && !defined(USE_SERVO_LIB)
// set timer 5 to 20 ms, since the servo library does not do this for us
    TCCR5A = _BV(WGM11);// FastPWM Mode mode TOP (20 ms) determined by ICR1 - non-inverting Compare Output mode OC1A+OC1B
    TCCR5B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);// set prescaler to 8, FastPWM mode mode bits WGM13 + WGM12
    ICR5 = (F_CPU / 8) / REFRESH_FREQUENCY; // 40000 - set period to 50 Hz / 20 ms
#    endif

    TIFR5 |= _BV(OCF5B);     // clear any pending interrupts;
    TIMSK5 |= _BV(OCIE5B);// enable the output compare B interrupt
    OCR5B = ((clockCyclesPerMicrosecond() * REFRESH_INTERVAL_MICROS) / 8) - 100;// update values 100 �s before the new servo period starts

#  elif defined(__AVR_ATmega4809__) || defined(__AVR_ATtiny3217__) // Uno WiFi Rev 2, Nano Every, Tiny Core 32 Dev Board
    // For MegaTinyCore:
    // TCB1 is used by Tone()
    // TCB2 is used by Servo, but we cannot hijack the ISR, so we must use a dedicated timer for the 20 ms interrupt
    // TCB3 is used by millis()
    // Must use TCA0, since TCBx have only prescaler %2. Use single (16bit) mode, because it seems to be easier :-)
    TCA0.SINGLE.CTRLD = 0; // Single mode - required at least for MegaTinyCore
    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_NORMAL_gc;                        // Normal mode, top = PER
    TCA0.SINGLE.PER =  (((F_CPU / 1000000) * REFRESH_INTERVAL_MICROS) / 8); // 40000 at 16 MHz
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV8_gc | TCA_SINGLE_ENABLE_bm;   // set prescaler to 8 and enable timer
    TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;                                // Enable overflow interrupt

#  elif defined(TCCR1B) && defined(TIFR1) // Uno, Nano etc.
    /*
     * Standard AVR
     * Use timer 1, together with the servo library, which uses the output compare A interrupt.
     * Therefore we use the output compare B interrupt and generate an interrupt 100 microseconds,
     * before a new servo period starts. This leaves the first servo signals undisturbed.
     */
#    if defined(USE_PCA9685_SERVO_EXPANDER) && !defined(USE_SERVO_LIB)
//    // set timer 1 to 20 ms, since the servo library does not do this for us
    TCCR1A = _BV(WGM11);     // FastPWM Mode mode TOP (20 ms) determined by ICR1 - non-inverting Compare Output mode OC1A+OC1B
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);     // set prescaler to 8, FastPWM mode mode bits WGM13 + WGM12
    ICR1 = (F_CPU / 8) / REFRESH_FREQUENCY; // 40000 - set period to 50 Hz / 20 ms
#    endif

    TIFR1 |= _BV(OCF1B);    // clear any pending interrupts;
    TIMSK1 |= _BV(OCIE1B);    // enable the output compare B interrupt
    /*
     * Misuse the Input Capture Noise Canceler Bit as a flag, that signals that interrupts for ServoEasing are enabled again.
     * It is required if disableServoEasingInterrupt() is suppressed e.g. by an overwritten handleServoTimerInterrupt() function
     * because the servo interrupt is used to synchronize e.g. NeoPixel updates.
     */
    TCCR1B |= _BV(ICNC1);
#    if !defined(USE_LEIGHTWEIGHT_SERVO_LIB)
    // Generate interrupt 100 �s before a new servo period starts
    OCR1B = ((clockCyclesPerMicrosecond() * REFRESH_INTERVAL_MICROS) / 8) - 100;
#    endif

#  else
#error "This AVR CPU is not supported by ServoEasing"
#  endif

#elif defined(ESP8266) || defined(ESP32)
    if(ServoEasing::sInterruptsAreActive) {
        Timer20ms.detach();     // otherwise the ESP32 kernel at least will crash and reboot
    }
    // It seems that the callback is called by a task not an ISR, which allow us to have the callback without the IRAM attribute
    Timer20ms.attach_ms(REFRESH_INTERVAL_MILLIS, handleServoTimerInterrupt);

// BluePill in 2 flavors
#elif defined(STM32F1xx)   // for "STM32:stm32" from STM32 Boards from STM32 cores of Arduino Board manager
    Timer20ms.setMode(LL_TIM_CHANNEL_CH1, TIMER_OUTPUT_COMPARE, NC);    // used for generating only interrupts, no pin specified
    Timer20ms.setOverflow(REFRESH_INTERVAL_MICROS, MICROSEC_FORMAT);    // microsecond period
    Timer20ms.attachInterrupt(handleServoTimerInterrupt);               // this sets update interrupt enable
    Timer20ms.resume();     // Start or resume HardwareTimer: all channels are resumed, interrupts are enabled if necessary

#elif defined(__STM32F1__)  // for "stm32duino:STM32F1 Generic STM32F103C series" from STM32F1 Boards (Roger Clark STM32duino.com)
    Timer20ms.setMode(TIMER_CH1, TIMER_OUTPUT_COMPARE);
    Timer20ms.setPeriod(REFRESH_INTERVAL_MICROS);   // 20000 microsecond period
    Timer20ms.attachInterrupt(TIMER_CH1, handleServoTimerInterrupt);
    Timer20ms.refresh();    // Set the timer's count to 0 and update the prescaler and overflow values.

#elif defined(__SAM3X8E__)  // Arduino DUE
    pmc_set_writeprotect(false);
    pmc_enable_periph_clk(ID_TC_FOR_20_MS_TIMER);
    NVIC_ClearPendingIRQ(IRQn_FOR_20_MS_TIMER);
    NVIC_EnableIRQ(IRQn_FOR_20_MS_TIMER);

    // TIMER_CLOCK3 is MCK/32. MCK is 84MHz Set up the Timer in waveform mode which creates a PWM in UP mode with automatic trigger on RC Compare
    TC_Configure(TC_FOR_20_MS_TIMER, CHANNEL_FOR_20_MS_TIMER, TC_CMR_TCCLKS_TIMER_CLOCK3 | TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC);
    TC_SetRC(TC_FOR_20_MS_TIMER, CHANNEL_FOR_20_MS_TIMER, (F_CPU / 32) / REFRESH_FREQUENCY); // =52500 -> 20ms

    TC_Start(TC_FOR_20_MS_TIMER, CHANNEL_FOR_20_MS_TIMER); // Enables the timer clock stopped by TC_Configure() and performs a software reset to start the counting

    // Enable the RC Compare Interrupt
    TC_FOR_20_MS_TIMER->TC_CHANNEL[CHANNEL_FOR_20_MS_TIMER].TC_IER = TC_IER_CPCS;
    // Disable all others
    TC_FOR_20_MS_TIMER->TC_CHANNEL[CHANNEL_FOR_20_MS_TIMER].TC_IDR = ~TC_IER_CPCS;

#elif defined(ARDUINO_ARCH_SAMD)
    // Servo uses timer 4 and we use timer 5. therefore we cannot change clock source to 32 kHz.
    // Enable GCLK for TCC2 and TC5 (timer counter input clock)
    GCLK->CLKCTRL.reg = (uint16_t) (GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID(GCM_TC4_TC5)); // GCLK1=32kHz,  GCLK0=48MHz
//    while (GCLK->STATUS.bit.SYNCBUSY) // not required to wait
//        ;

    // The TC should be disabled before the TC is reset in order to avoid undefined behavior.
    TC5->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
    // 14.3.2.2 When write-synchronization is ongoing for a register, any subsequent write attempts to this register will be discarded, and an error will be reported.
    // 14.3.1.4 It is also possible to perform the next read/write operation and wait,
    // as this next operation will be started once the previous write/read operation is synchronized and/or complete. ???
    while (TC5->COUNT16.STATUS.bit.SYNCBUSY == 1); // wait for sync
    // Reset TCx
    TC5->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
    // When writing a �1� to the CTRLA.SWRST bit it will immediately read as �1�.
    // CTRL.SWRST will be cleared by hardware when the peripheral has been reset.
    while (TC5->COUNT16.CTRLA.bit.SWRST)
        ;

    /*
     * Set timer counter mode to 16 bits, set mode as match frequency, prescaler is DIV64 => 750 kHz clock, start counter
     */
    TC5->COUNT16.CTRLA.reg |= TC_CTRLA_MODE_COUNT16| TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV64 | TC_CTRLA_ENABLE;
    TC5->COUNT16.CC[0].reg = (uint16_t) (((F_CPU/64) / REFRESH_FREQUENCY) - 1);     // (750 kHz / sampleRate - 1);
//    while (TC5->COUNT16.STATUS.bit.SYNCBUSY == 1)                                 // The next commands do an implicit wait :-)
//        ;

    // Configure interrupt request
    NVIC_DisableIRQ(TC5_IRQn);
    NVIC_ClearPendingIRQ(TC5_IRQn);
    NVIC_SetPriority(TC5_IRQn, 0);
    NVIC_EnableIRQ(TC5_IRQn);

    // Enable the TC5 interrupt request
    TC5->COUNT16.INTENSET.bit.MC0 = 1;
//    while (TC5->COUNT16.STATUS.reg & TC_STATUS_SYNCBUSY) // Not required to wait at end of function
//        ; // wait until TC5 is done syncing

//#elif defined(ARDUINO_ARCH_APOLLO3)
//    // use timer 3 segment A
//    am_hal_ctimer_clear(3, AM_HAL_CTIMER_TIMERA); // reset timer
//    // only AM_HAL_CTIMER_FN_REPEAT resets counter after match (CTC mode)
//    am_hal_ctimer_config_single(3, AM_HAL_CTIMER_TIMERA,
//            (AM_HAL_CTIMER_INT_ENABLE | AM_HAL_CTIMER_HFRC_12KHZ | AM_HAL_CTIMER_FN_REPEAT));
//    am_hal_ctimer_compare_set(3, AM_HAL_CTIMER_TIMERA, 0, 12000 / REFRESH_FREQUENCY);
//    am_hal_ctimer_start(3, AM_HAL_CTIMER_TIMERA);
//
//    am_hal_ctimer_int_register(AM_HAL_CTIMER_INT_TIMERA3, handleServoTimerInterrupt);
//    am_hal_ctimer_int_enable(AM_HAL_CTIMER_INT_TIMERA3);
//    NVIC_EnableIRQ(CTIMER_IRQn);

#elif defined(ARDUINO_ARCH_MBED)
    Timer20ms.attach(handleServoTimerInterrupt, std::chrono::microseconds(REFRESH_INTERVAL_MICROS));

#elif defined(ARDUINO_ARCH_RP2040)
    add_repeating_timer_us(REFRESH_INTERVAL_MICROS, handleServoTimerInterruptHelper, NULL, &Timer20ms);

#elif defined(TEENSYDUINO)
    // common for all Teensy
    Timer20ms.begin(handleServoTimerInterrupt, REFRESH_INTERVAL_MICROS);

#else
#warning Board / CPU is not covered by definitions using pre-processor symbols -> no timer available. Please extend ServoEasing.cpp.
#endif
    ServoEasing::sInterruptsAreActive = true;
}

#if defined(__AVR_ATmega328P__)
/*
 * To have more time for overwritten interrupt routine to handle its task.
 */
void setTimer1InterruptMarginMicros(uint16_t aInterruptMarginMicros){
    // Generate interrupt aInterruptMarginMicros �s before a new servo period starts
    OCR1B = ((clockCyclesPerMicrosecond() * REFRESH_INTERVAL_MICROS) / 8) - aInterruptMarginMicros;
}
#endif

void disableServoEasingInterrupt() {
#if defined(__AVR__)
#  if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    TIMSK5 &= ~(_BV(OCIE5B)); // disable the output compare B interrupt

#  elif defined(__AVR_ATmega4809__) || defined(__AVR_ATtiny3217__) // Uno WiFi Rev 2, Nano Every, Tiny Core 32 Dev Board
    TCA0.SINGLE.INTCTRL &= ~(TCA_SINGLE_OVF_bm); // disable the overflow interrupt

#  elif defined(TIMSK1)// defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    TIMSK1 &= ~(_BV(OCIE1B)); // disable the output compare B interrupt

#  else
#error "This AVR CPU is not supported by ServoEasing"
#  endif

#elif defined(ESP8266) || defined(ESP32)
    Timer20ms.detach();

#elif defined(STM32F1xx)   // for "Generic STM32F1 series" from STM32 Boards from STM32 cores of Arduino Board manager
    // https://github.com/stm32duino/BoardManagerFiles/raw/master/STM32/package_stm_index.json
    Timer20ms.setMode(LL_TIM_CHANNEL_CH1, TIMER_DISABLED);
    Timer20ms.detachInterrupt();

#elif defined(__STM32F1__) // for "Generic STM32F103C series" from STM32F1 Boards (STM32duino.com) of Arduino Board manager
    // http://dan.drown.org/stm32duino/package_STM32duino_index.json
    Timer20ms.setMode(TIMER_CH1, TIMER_DISABLED);
    Timer20ms.detachInterrupt(TIMER_CH1);

#elif defined(__SAM3X8E__)  // Arduino DUE
    NVIC_DisableIRQ(IRQn_FOR_20_MS_TIMER);

#elif defined(ARDUINO_ARCH_SAMD)
    TC5->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
    while (TC5->COUNT16.STATUS.reg & TC_STATUS_SYNCBUSY); //wait until TC5 is done syncing

#elif defined(ARDUINO_ARCH_MBED) // Arduino Nano 33 BLE + Sparkfun Apollo3
    Timer20ms.detach();

#elif defined(ARDUINO_ARCH_RP2040)
    cancel_repeating_timer(&Timer20ms);

//#elif defined(ARDUINO_ARCH_APOLLO3)
//    am_hal_ctimer_int_disable(AM_HAL_CTIMER_INT_TIMERA3);

#elif defined(TEENSYDUINO)
    Timer20ms.end();
#endif
    ServoEasing::sInterruptsAreActive = false;
}

// @formatter:on
/*
 * 60 �s for single servo + 160 �s per servo if using I2C e.g.for PCA9685 expander at 400 kHz or + 100 at 800 kHz
 * 20 �s for last interrupt
 * The first servo pulse starts just after this interrupt routine has finished
 */
#if defined(__AVR__)
#  if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
ISR(TIMER5_COMPB_vect) {
    handleServoTimerInterrupt();
}

#  elif defined(__AVR_ATmega4809__) || defined(__AVR_ATtiny3217__) // Uno WiFi Rev 2, Nano Every, Tiny Core 32 Dev Board
ISR(TCA0_OVF_vect) {
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm; // Reset interrupt flags.
    handleServoTimerInterrupt();
}

#  else // defined(__AVR__)
ISR(TIMER1_COMPB_vect) {
#    if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    digitalWriteFast(TIMING_OUTPUT_PIN, HIGH);
#    endif
    handleServoTimerInterrupt();
#    if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    digitalWriteFast(TIMING_OUTPUT_PIN, LOW);
#    endif
}
#  endif

#elif defined(__SAM3X8E__)  // Arduino DUE
void HANDLER_FOR_20_MS_TIMER(void) {
#  if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    digitalWrite(TIMING_OUTPUT_PIN, HIGH);
#  endif
    // Clear interrupt
    TC_GetStatus(TC_FOR_20_MS_TIMER, CHANNEL_FOR_20_MS_TIMER);//Clear channel status to fire again the interrupt.
    handleServoTimerInterrupt();
#  if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    digitalWrite(TIMING_OUTPUT_PIN, LOW);
#  endif
}

#elif defined(ARDUINO_ARCH_SAMD)
void TC5_Handler(void) {
#  if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    digitalWrite(TIMING_OUTPUT_PIN, HIGH);
#  endif
    // Clear interrupt
    TC5->COUNT16.INTFLAG.bit.MC0 = 1;
    handleServoTimerInterrupt();
#  if defined(MEASURE_SERVO_EASING_INTERRUPT_TIMING)
    digitalWrite(TIMING_OUTPUT_PIN, LOW);
#  endif
}

//#elif defined(ARDUINO_ARCH_APOLLO3)
//extern "C" void am_ctimer_isr(void) {
//    // Check and clear any active CTIMER interrupts.
//    uint32_t ui32Status = am_hal_ctimer_int_status_get(true);
//    am_hal_ctimer_int_clear(ui32Status);
//
//    // Run handlers for the various possible timer events.
//    am_hal_ctimer_int_service(ui32Status);
//}
#endif // defined(__AVR__)

/************************************
 * ServoEasing list functions
 ***********************************/

#if !defined(PROVIDE_ONLY_LINEAR_MOVEMENT)
void setEasingTypeForAllServos(uint_fast8_t aEasingType) {
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            ServoEasing::ServoEasingArray[tServoIndex]->mEasingType = aEasingType;
        }
    }
}
#endif

void setEaseToForAllServosSynchronizeAndStartInterrupt() {
    setEaseToForAllServos();
    synchronizeAllServosAndStartInterrupt();
}

void setEaseToForAllServosSynchronizeAndStartInterrupt(uint_fast16_t aDegreesPerSecond) {
    setEaseToForAllServos(aDegreesPerSecond);
    synchronizeAllServosAndStartInterrupt();
}

void synchronizeAndEaseToArrayPositions() {
    setEaseToForAllServos();
    synchronizeAllServosStartAndWaitForAllServosToStop();
}

void synchronizeAndEaseToArrayPositions(uint_fast16_t aDegreesPerSecond) {
    setEaseToForAllServos(aDegreesPerSecond);
    synchronizeAllServosStartAndWaitForAllServosToStop();
}

/**
 * Prints content of ServoNextPositionArray for debugging purposes.
 * @param aSerial The Print object on which to write, for Arduino you can use &Serial.
 */
void printArrayPositions(Print *aSerial) {
//    uint_fast8_t tServoIndex = 0;
    aSerial->print(F("ServoNextPositionArray="));
// AJ 22.05.2019 This does not work with GCC 7.3.0 atmel6.3.1 and -Os
// It drops the tServoIndex < MAX_EASING_SERVOS condition, since  MAX_EASING_SERVOS is equal to the size of sServoArray
// This has only an effect if the whole sServoArray is filled up, i.e we have declared MAX_EASING_SERVOS ServoEasing objects.
//    while (ServoEasing::ServoEasingArray[tServoIndex] != NULL && tServoIndex < MAX_EASING_SERVOS) {
//        aSerial->print(ServoEasingNextPositionArray[tServoIndex]);
//        aSerial->print(F(" | "));
//        tServoIndex++;
//    }

// switching conditions cures the bug
//    while (tServoIndex < MAX_EASING_SERVOS && ServoEasing::ServoEasingArray[tServoIndex] != NULL) {

// this also does not work
//    for (uint_fast8_t tServoIndex = 0; ServoEasing::ServoEasingArray[tServoIndex] != NULL && tServoIndex < MAX_EASING_SERVOS  ; ++tServoIndex) {
//        aSerial->print(ServoEasingNextPositionArray[tServoIndex]);
//        aSerial->print(F(" | "));
//    }
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        aSerial->print(ServoEasing::ServoEasingNextPositionArray[tServoIndex]);
        aSerial->print(F(" | "));
    }
    aSerial->println();
}

void writeAllServos(int aDegreeOrMicrosecond) {
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            ServoEasing::ServoEasingArray[tServoIndex]->write(aDegreeOrMicrosecond);
        }
    }
}

void setSpeedForAllServos(uint_fast16_t aDegreesPerSecond) {
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            ServoEasing::ServoEasingArray[tServoIndex]->mSpeed = aDegreesPerSecond;
        }
    }
}

#if defined(va_arg)
/*
 * Sets the ServoEasingNextPositionArray[] of the first aNumberOfServos to the specified values
 */
void setDegreeForAllServos(uint_fast8_t aNumberOfServos, va_list *aDegreeValues) {
    for (uint_fast8_t tServoIndex = 0; tServoIndex < aNumberOfServos; ++tServoIndex) {
        ServoEasing::ServoEasingNextPositionArray[tServoIndex] = va_arg(*aDegreeValues, int);
    }
}
#endif

#if defined(va_start)
/*
 * Sets the ServoEasingNextPositionArray[] of the first aNumberOfServos to the specified values
 */
void setDegreeForAllServos(uint_fast8_t aNumberOfServos, ...) {
    va_list aDegreeValues;
    va_start(aDegreeValues, aNumberOfServos);
    setDegreeForAllServos(aNumberOfServos, &aDegreeValues);
    va_end(aDegreeValues);
}
#endif

/*
 * Sets target position using content of ServoEasingNextPositionArray
 * returns false if one servo was still moving
 */
bool setEaseToForAllServos() {
    bool tOneServoIsMoving = false;
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            tOneServoIsMoving = ServoEasing::ServoEasingArray[tServoIndex]->setEaseTo(
                    ServoEasing::ServoEasingNextPositionArray[tServoIndex], ServoEasing::ServoEasingArray[tServoIndex]->mSpeed)
                    || tOneServoIsMoving;
        }
    }
    return tOneServoIsMoving;
}

bool setEaseToForAllServos(uint_fast16_t aDegreesPerSecond) {
    bool tOneServoIsMoving = false;
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            tOneServoIsMoving = ServoEasing::ServoEasingArray[tServoIndex]->setEaseTo(
                    ServoEasing::ServoEasingNextPositionArray[tServoIndex], aDegreesPerSecond) || tOneServoIsMoving;
        }
    }
    return tOneServoIsMoving;
}

bool setEaseToDForAllServos(uint_fast16_t aMillisForMove) {
    bool tOneServoIsMoving = false;
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            tOneServoIsMoving = ServoEasing::ServoEasingArray[tServoIndex]->setEaseToD(
                    ServoEasing::ServoEasingNextPositionArray[tServoIndex], aMillisForMove) || tOneServoIsMoving;
        }
    }
    return tOneServoIsMoving;
}

bool isOneServoMoving() {
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL && ServoEasing::ServoEasingArray[tServoIndex]->mServoMoves) {
            return true;
        }
    }
    return false;
}

void stopAllServos() {
    void disableServoEasingInterrupt();
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            ServoEasing::ServoEasingArray[tServoIndex]->mServoMoves = false;
        }
    }
}

/*
 * returns true if all Servos reached endAngle / stopped
 */
bool updateAllServos() {
    bool tAllServosStopped = true;
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL) {
            tAllServosStopped = ServoEasing::ServoEasingArray[tServoIndex]->update() && tAllServosStopped;
        }
    }
#if defined(PRINT_FOR_SERIAL_PLOTTER)
// End of one data set
        Serial.println();
#endif
    return tAllServosStopped;
}

void updateAndWaitForAllServosToStop() {
    do {
        // First do the delay, then check for update, since we are likely called directly after start and there is nothing to move yet
        delay(REFRESH_INTERVAL_MILLIS); // 20 ms
    } while (!updateAllServos());
}

/*
 * @param aMillisDelay the milliseconds for blocking wait and update
 * @param aTerminateDelayIfAllServosStopped if true, function returns before aMillisDelay if all servos are stopped
 * returns true if all Servos reached endAngle / stopped
 */
bool delayAndUpdateAndWaitForAllServosToStop(unsigned long aMillisDelay, bool aTerminateDelayIfAllServosStopped) {
    while (true) {
        // First do the delay, then check for update, since we are likely called directly after start and there is nothing to move yet
        if (aMillisDelay > REFRESH_INTERVAL_MILLIS) {
            aMillisDelay -= REFRESH_INTERVAL_MILLIS;
            delay(REFRESH_INTERVAL_MILLIS); // 20 ms
            if (updateAllServos() && aTerminateDelayIfAllServosStopped) {
                // terminate delay here and return
                return true;
            }
        } else {
            delay(aMillisDelay);
            return updateAllServos();
        }
    }
}

void synchronizeAllServosStartAndWaitForAllServosToStop() {
    synchronizeAllServosAndStartInterrupt(false);
    updateAndWaitForAllServosToStop();
}

/*
 * Take the longer duration in order to move all servos synchronously
 */
void synchronizeAllServosAndStartInterrupt(bool aStartUpdateByInterrupt) {
    /*
     * Find maximum duration and one start time
     */
    uint_fast16_t tMaxMillisForCompleteMove = 0;
    uint32_t tMillisAtStartMove = 0;

    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL && ServoEasing::ServoEasingArray[tServoIndex]->mServoMoves) {
            //process servos which really moves
            tMillisAtStartMove = ServoEasing::ServoEasingArray[tServoIndex]->mMillisAtStartMove;
            if (ServoEasing::ServoEasingArray[tServoIndex]->mMillisForCompleteMove > tMaxMillisForCompleteMove) {
                tMaxMillisForCompleteMove = ServoEasing::ServoEasingArray[tServoIndex]->mMillisForCompleteMove;
            }
        }
    }

#if defined(TRACE)
        Serial.print(F("Number of servos="));
        Serial.print(ServoEasing::sServoArrayMaxIndex);
        Serial.print(F(" MillisAtStartMove="));
        Serial.print(tMillisAtStartMove);
        Serial.print(F(" MaxMillisForCompleteMove="));
        Serial.println(tMaxMillisForCompleteMove);
#endif

    /*
     * Set maximum duration and start time to all servos
     * Synchronize start time to avoid race conditions at the end of movement
     */
    for (uint_fast8_t tServoIndex = 0; tServoIndex <= ServoEasing::sServoArrayMaxIndex; ++tServoIndex) {
        if (ServoEasing::ServoEasingArray[tServoIndex] != NULL && ServoEasing::ServoEasingArray[tServoIndex]->mServoMoves) {
            ServoEasing::ServoEasingArray[tServoIndex]->mMillisAtStartMove = tMillisAtStartMove;
            ServoEasing::ServoEasingArray[tServoIndex]->mMillisForCompleteMove = tMaxMillisForCompleteMove;
        }
    }

    if (aStartUpdateByInterrupt) {
        enableServoEasingInterrupt();
    }
}

/*********************************************************
 * Included easing functions
 * Input is from 0.0 to 1.0 with 0.0 -> 0 % and 1.0 -> 100% completion of time between the two endpoints
 * Output is from 0.0 to 1.0 with: 0.0 -> 0 % and 1.0 -> 100% completion of movement (e.g. 1.1 is 10% overshot)
 ********************************************************/
float (*sEaseFunctionArray[])(
        float aFactorOfTimeCompletion) = {&QuadraticEaseIn, &CubicEaseIn, &QuarticEaseIn, &SineEaseIn, &CircularEaseIn, &BackEaseIn, &ElasticEaseIn,
            &EaseOutBounce};
/*
 * The simplest non linear easing function
 */
float QuadraticEaseIn(float aFactorOfTimeCompletion) {
    return (aFactorOfTimeCompletion * aFactorOfTimeCompletion);
}

float CubicEaseIn(float aFactorOfTimeCompletion) {
    return (aFactorOfTimeCompletion * QuadraticEaseIn(aFactorOfTimeCompletion));
}

float QuarticEaseIn(float aFactorOfTimeCompletion) {
    return QuadraticEaseIn(QuadraticEaseIn(aFactorOfTimeCompletion));
}

/*
 * Take half of negative cosines of first quadrant
 * Is behaves almost like QUADRATIC
 */
float SineEaseIn(float aFactorOfTimeCompletion) {
    return sin((aFactorOfTimeCompletion - 1) * M_PI_2) + 1;
}

/*
 * It is very fast in the middle!
 * see: https://easings.net/#easeInOutCirc
 * and https://github.com/warrenm/AHEasing/blob/master/AHEasing/easing.c
 */
float CircularEaseIn(float aFactorOfTimeCompletion) {
    return 1 - sqrt(1 - (aFactorOfTimeCompletion * aFactorOfTimeCompletion));
}

/*
 * see: https://easings.net/#easeInOutBack
 * and https://github.com/warrenm/AHEasing/blob/master/AHEasing/easing.c
 */
float BackEaseIn(float aFactorOfTimeCompletion) {
    return (aFactorOfTimeCompletion * aFactorOfTimeCompletion * aFactorOfTimeCompletion)
            - (aFactorOfTimeCompletion * sin(aFactorOfTimeCompletion * M_PI));
}

/*
 * see: https://easings.net/#easeInOutElastic
 * and https://github.com/warrenm/AHEasing/blob/master/AHEasing/easing.c
 */
float ElasticEaseIn(float aFactorOfTimeCompletion) {
    return sin(13 * M_PI_2 * aFactorOfTimeCompletion) * pow(2, 10 * (aFactorOfTimeCompletion - 1));
}

/*
 * !!! ATTENTION !!! we have only the out function implemented
 * see: https://easings.net/de#easeOutBounce
 * and https://github.com/warrenm/AHEasing/blob/master/AHEasing/easing.c
 */
float EaseOutBounce(float aFactorOfTimeCompletion) {
    float tFactorOfMovementCompletion;
    if (aFactorOfTimeCompletion < 4 / 11.0) {
        tFactorOfMovementCompletion = (121 * aFactorOfTimeCompletion * aFactorOfTimeCompletion) / 16.0;
    } else if (aFactorOfTimeCompletion < 8 / 11.0) {
        tFactorOfMovementCompletion = (363 / 40.0 * aFactorOfTimeCompletion * aFactorOfTimeCompletion)
                - (99 / 10.0 * aFactorOfTimeCompletion) + 17 / 5.0;
    } else if (aFactorOfTimeCompletion < 9 / 10.0) {
        tFactorOfMovementCompletion = (4356 / 361.0 * aFactorOfTimeCompletion * aFactorOfTimeCompletion)
                - (35442 / 1805.0 * aFactorOfTimeCompletion) + 16061 / 1805.0;
    } else {
        tFactorOfMovementCompletion = (54 / 5.0 * aFactorOfTimeCompletion * aFactorOfTimeCompletion)
                - (513 / 25.0 * aFactorOfTimeCompletion) + 268 / 25.0;
    }
    return tFactorOfMovementCompletion;
}

/************************************
 * Convenience I2C check function
 * One version as class methods and one version as static function
 ***********************************/
#if defined(USE_PCA9685_SERVO_EXPANDER)
/*
 * Check if I2C communication is possible. If not, we will wait forever at endTransmission.
 * 0x40 is default PCA9685 address
 * @param aSerial The Print object on which to write, for Arduino you can use &Serial.
 * @return true if error happened, i.e. device is not attached at this address.
 */
#if defined(__AVR__)
bool ServoEasing::InitializeAndCheckI2CConnection(Print *aSerial) // Print instead of Stream saves 95 bytes flash
#else
bool ServoEasing::InitializeAndCheckI2CConnection(Stream *aSerial) // Print has no flush()
#endif
{
#if !defined(USE_SOFT_I2C_MASTER)
    // Initialize wire before checkI2CConnection()
    I2CInit();
#endif
    return checkI2CConnection(mPCA9685I2CAddress, aSerial);
}

#if defined(__AVR__)
bool checkI2CConnection(uint8_t aI2CAddress, Print *aSerial) // Print instead of Stream saves 95 bytes flash
#else
bool checkI2CConnection(uint8_t aI2CAddress, Stream *aSerial) // Print has no flush()
#endif
        {

    bool tRetValue = false;
    aSerial->print(F("Try to communicate with I2C device at address=0x"));
    aSerial->println(aI2CAddress, HEX);
    aSerial->flush();

    // Initialize wire
#if defined(USE_SOFT_I2C_MASTER)
    if(i2c_init()){
        if(!i2c_start(aI2CAddress << 1)){
            aSerial->println(F("No acknowledge received from the slave"));
            aSerial->print(F("Communication with I2C was successful, but found no"));
            tRetValue = true;
        } else {
            aSerial->print(F("Found"));
        }
        i2c_stop();
        aSerial->print(F(" I2C device attached at address: 0x"));
        aSerial->println(aI2CAddress, HEX);
    } else {
        aSerial->println(F("I2C init failed"));
    }
#else // defined(USE_SOFT_I2C_MASTER)

#  if defined (ARDUINO_ARCH_AVR) // Other platforms do not have this new function
    do {
        Wire.beginTransmission(aI2CAddress);
        if (Wire.getWireTimeoutFlag()) {
            aSerial->println(F("Timeout accessing I2C bus. Wait for bus becoming available"));
            Wire.clearWireTimeoutFlag();
            delay(100);
        } else {
            break;
        }
    } while (true);
#  else
    Wire.beginTransmission(aI2CAddress);
#  endif

    uint8_t tWireReturnCode = Wire.endTransmission(true);
    if (tWireReturnCode == 0) {
        aSerial->print(F("Found"));
    } else {
        aSerial->print(F("Error code="));
        aSerial->print(tWireReturnCode);
        aSerial->print(F(". Communication with I2C was successful, but found no"));
        tRetValue = true;
    }
    aSerial->print(F(" I2C device attached at address: 0x"));
    aSerial->println(aI2CAddress, HEX);
#endif // defined(USE_SOFT_I2C_MASTER)

    if(tRetValue) {
        aSerial->println(F("PCA9685 expander not connected"));
    }
    return tRetValue;
}
# endif // defined(USE_PCA9685_SERVO_EXPANDER)

#endif // _SERVO_EASING_HPP
#pragma once
