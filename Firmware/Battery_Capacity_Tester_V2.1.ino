/*
====================================================================================================================
  PROJECT      : DIY Arduino Battery Capacity Tester V2.1
  VERSION      : v2.1
  UPDATED ON   : 02-Feb-2022
  AUTHOR       : Open Green Energy

  ORIGINAL BASIS
  ------------------------------------------------------------------------------------------------------------------
  This code is based on the earlier battery capacity tester work and includes modifications for improved current
  calculation using measured Vcc and band gap reference input.

  LICENSE
  ------------------------------------------------------------------------------------------------------------------
  Copyright (c) 2026 Open Green Energy

  Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
  https://creativecommons.org/licenses/by-nc-sa/4.0/

  HARDWARE USED
  ------------------------------------------------------------------------------------------------------------------
  MCU            : Arduino compatible board
  DISPLAY        : OLED 128x64 SSD1306 (I2C)
  INPUTS         : UP and DOWN push buttons
  LOAD CONTROL   : PWM controlled discharge MOSFET / transistor stage
  SENSING        : Battery voltage on A0, band gap reference on A1
  BUZZER         : Status buzzer

  WHAT THIS DEVICE DOES
  ------------------------------------------------------------------------------------------------------------------
  1. Allows user selection of discharge current using UP and DOWN buttons.
  2. Measures battery voltage during discharge.
  3. Discharges the battery until cutoff voltage is reached.
  4. Calculates elapsed time and battery capacity in mAh.
  5. Displays current, battery voltage, elapsed time, and final capacity on OLED.
  6. Uses external band gap reference on A1 to calculate actual Vcc.

====================================================================================================================
*/

#include <JC_Button.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// DEBUG CONFIGURATION
// 0 = Off
// 1 = Startup debug
// 2 = Running voltages
// 4 = Finish debug
// 5 = Combination modes used in original code
// ============================================================================
#define DEBUG 0

// ============================================================================
// OLED DISPLAY CONFIGURATION
// ============================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 4

// SSD1306 display object using I2C.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
void timerInterrupt();
void Display_UP_DOWN();
void Print_DEBUG_4();

// ============================================================================
// BATTERY TEST SETTINGS
// ============================================================================
const float Low_BAT_level = 2.80;   // Battery cutoff voltage

// Desired discharge current steps for the load stage.
int Current[] = {0, 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};

// Initial placeholder PWM values. These are recalculated during setup using measured Vcc.
int PWM[] = {0, 2, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50};

int Array_Size;   // Calculated during setup

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
const byte PWM_Pin = 10;
const byte Buzzer  = 9;
const int BAT_Pin  = A0;
const int Vref_Pin = A1;   // External band gap reference input

// ============================================================================
// RUNTIME VARIABLES
// ============================================================================
int Current_Value = 0;   // Actual selected discharge current
int PWM_Value     = 0;   // PWM output during test
int PWM_Index     = 0;   // Current index into Current[] and PWM[]

unsigned long Capacity = 0;
float Capacity_f = 0.0f;

int ADC_Value = 0;
float Vref_Voltage = 1.215;   // LM385BLP-1.2 band gap voltage
float Vcc = 5.32;             // Arduino supply voltage, recalculated in setup
float BAT_Voltage = 0.0f;
float Resistance = 1.0f;      // Load resistor value
float sample = 0.0f;

byte Hour = 0, Minute = 0, Second = 0;

bool calc = false;
bool Done = false;
bool Report_Info = true;

// ============================================================================
// BUTTON OBJECTS
// ============================================================================
Button UP_Button(2, 25, false, true);
Button Down_Button(3, 25, false, true);

// ============================================================================
// STRING BUFFERS FOR STARTUP REPORTING
// ============================================================================
const int VAL_MAX = 10;
char val_0[VAL_MAX] = {""};
char val_2[VAL_MAX] = {""};

// ============================================================================
// SETUP
// Initializes display, buttons, PWM output, measures Vcc, and recalculates
// the actual current and PWM tables.
// ============================================================================
void setup() {
  Serial.begin(38400);

  pinMode(PWM_Pin, OUTPUT);
  pinMode(Buzzer, OUTPUT);

  analogWrite(PWM_Pin, PWM_Value);

  UP_Button.begin();
  Down_Button.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(12, 25);
  display.print("Open Green Energy");
  display.display();
  delay(3000);

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(2, 15);
  display.print("Adj Curr:");
  display.setCursor(2, 40);
  display.print("UP/Down:");
  display.print("0");
  display.setCursor(2, 55);
  display.setTextSize(1);

#if (DEBUG == 1 || DEBUG == 5)
  Serial.println("\nStart of calculations");
#endif

  // Number of current steps in the Current[] table.
  Array_Size = sizeof(Current) / 2;

  // --------------------------------------------------------------------------
  // Measure actual Vcc using external band gap reference on A1
  // --------------------------------------------------------------------------
  float fTemp = 0.0f;
  for (int i = 0; i < 100; i++) {
    fTemp = fTemp + analogRead(Vref_Pin);
    delay(2);
  }
  fTemp = fTemp / 100.0f;
  Vcc = Vref_Voltage * 1024.0f / fTemp;

  // --------------------------------------------------------------------------
  // Recalculate PWM values from desired current using actual measured Vcc
  // Then convert back to actual current values for more accurate display
  // --------------------------------------------------------------------------
  int iTemp;
  for (int i = 0; i < Array_Size; i++) {
    iTemp = int(Resistance * Current[i] * 256.0 / Vcc / 1000.0 - 1.0 + 0.5);
    iTemp = min(iTemp, 255);
    iTemp = max(iTemp, 0);

    Current[i] = int((iTemp + 1) * Vcc / 256.0 / Resistance * 1000.0);
    PWM[i] = iTemp;
  }

  // --------------------------------------------------------------------------
  // Show battery threshold and measured Vcc on startup screen
  // --------------------------------------------------------------------------
  dtostrf(Low_BAT_level, 5, 3, val_0);
  dtostrf(Vcc, 5, 3, val_2);

  display.print("Thr=");
  display.print(val_0);
  display.print("v, Vcc=");
  display.print(val_2);

  display.display();
  display.setTextSize(2);
}

// ============================================================================
// LOOP
// Handles current selection and starts the discharge test when the UP button
// is held for 1 second.
// ============================================================================
void loop() {
  if (Report_Info) {
    Serial.flush();

#if (DEBUG == 1 || DEBUG == 5)
    Serial.print("Threshold: ");
    Serial.print(Low_BAT_level, 3);
    Serial.println(" volts");

    Serial.print("R3 Resistance: ");
    Serial.print(Resistance, 3);
    Serial.println(" ohms");

    Serial.print("Measured Vcc Voltage: ");
    Serial.print(Vcc, 4);
    Serial.println(" volts");

    sample = 0.0f;

    Serial.println("Index Actual(mA) PWM");
    for (int i = 0; i < Array_Size; i++) {
      Serial.print("[");
      Serial.print(i);
      Serial.print("] ");
      Serial.print(Current[i], DEC);
      Serial.print(" ");
      Serial.print(PWM[i], DEC);
      Serial.println(" ");
    }
#endif

    Report_Info = false;
  }

  UP_Button.read();
  Down_Button.read();

  // Increase current step while test is not running
  if (UP_Button.wasReleased() && PWM_Index < (Array_Size - 1) && calc == false) {
    PWM_Value = PWM[++PWM_Index];
    analogWrite(PWM_Pin, PWM_Value);
    Display_UP_DOWN();
  }

  // Decrease current step while test is not running
  if (Down_Button.wasReleased() && PWM_Index > 0 && calc == false) {
    PWM_Value = PWM[--PWM_Index];
    analogWrite(PWM_Pin, PWM_Value);
    Display_UP_DOWN();
  }

  // Start test if UP button is held for 1 second
  if (UP_Button.pressedFor(1000) && calc == false) {
    digitalWrite(Buzzer, HIGH);
    delay(100);
    digitalWrite(Buzzer, LOW);

    display.clearDisplay();
    timerInterrupt();
  }
}

// ============================================================================
// TIMERINTERRUPT
// Original main discharge test routine.
// Tracks elapsed time, measures battery voltage, calculates capacity,
// and stops the test when battery voltage falls below cutoff.
// ============================================================================
void timerInterrupt() {
  calc = true;

  while (Done == false) {
    // ------------------------------------------------------------------------
    // Update elapsed time counters
    // ------------------------------------------------------------------------
    Second++;
    if (Second == 60) {
      Second = 0;
      Minute++;
    }

    if (Minute == 60) {
      Minute = 0;
      Hour++;
    }

    // ------------------------------------------------------------------------
    // Measure battery voltage using 100 ADC samples
    // ------------------------------------------------------------------------
    sample = 0;   // Reset accumulator
    for (int i = 0; i < 100; i++) {
      sample = sample + analogRead(BAT_Pin);
      delay(2);
    }

    sample = sample / 100;
    BAT_Voltage = sample * Vcc / 1024.0;

    // ------------------------------------------------------------------------
    // Display elapsed time, discharge current, battery voltage, and capacity
    // ------------------------------------------------------------------------
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(20, 5);
    display.print(Hour);
    display.print(":");
    display.print(Minute);
    display.print(":");
    display.print(Second);

    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Disch Curr: ");
    display.print(Current_Value);
    display.print("mA");

    display.setCursor(2, 40);
    display.print("Bat Volt:");
    display.print(BAT_Voltage, 3);
    display.print("V");

    // Capacity calculation in seconds converted to mAh
    Capacity = ((unsigned long)Hour * 3600) + ((unsigned long)Minute * 60) + (unsigned long)Second;
    Capacity_f = ((float)Capacity * Current_Value) / 3600.0;

    display.setCursor(2, 55);
    display.print("Capacity:");
    display.print(Capacity_f, 1);
    display.print("mAh");
    display.display();

#if (DEBUG == 4 || DEBUG == 2)
    Print_DEBUG_4();
#endif

    // ------------------------------------------------------------------------
    // Stop test when battery voltage falls below cutoff threshold
    // ------------------------------------------------------------------------
    if (BAT_Voltage < Low_BAT_level) {

#if (DEBUG == 4 || DEBUG == 5)
      Serial.println("\nCurrent_Value PWM_Value");
      Serial.print(Current_Value);
      Serial.print(" ");
      Serial.println(PWM_Value);

      Serial.println("\nHour Minute Second PWM_Index");
      Serial.print(Hour);
      Serial.print(" ");
      Serial.print(Minute);
      Serial.print(" ");
      Serial.print(Second);
      Serial.print(" ");
      Serial.println(PWM_Index);
#endif

      Capacity = ((unsigned long)Hour * 3600) + ((unsigned long)Minute * 60) + (unsigned long)Second;

#if (DEBUG == 4 || DEBUG == 5)
      Serial.println("Capacity HMS");
      Serial.println(Capacity);
#endif

      Capacity_f = ((float)Capacity * Current_Value) / 3600.0;

#if (DEBUG == 4 || DEBUG == 5)
      Serial.println("Capacity HMS*PWM");
      Serial.println(Capacity_f, 1);
#endif

      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(2, 15);
      display.print("Capacity:");
      display.setCursor(2, 40);
      display.print(Capacity_f, 1);
      display.print("mAh");
      display.display();

      Done = true;
      PWM_Value = 0;
      analogWrite(PWM_Pin, PWM_Value);

      // Completion beep pattern
      digitalWrite(Buzzer, HIGH);
      delay(100);
      digitalWrite(Buzzer, LOW);
      delay(100);
      digitalWrite(Buzzer, HIGH);
      delay(100);
      digitalWrite(Buzzer, LOW);
      delay(100);
    }

    delay(1000);
  }
}

// ============================================================================
// DISPLAY_UP_DOWN
// Shows selected current and PWM value while user adjusts the current step.
// ============================================================================
void Display_UP_DOWN() {
  Current_Value = Current[PWM_Index];

  display.clearDisplay();
  display.setCursor(2, 25);
  display.print("Curr:");
  display.print(Current_Value);
  display.print("mA ");

  display.setCursor(2, 40);
  display.print("PWM=");
  display.print(PWM_Value);
  display.display();
}

// ============================================================================
// PRINT_DEBUG_4
// Prints running debug information to Serial Monitor.
// ============================================================================
void Print_DEBUG_4() {
  Serial.print(Hour);
  Serial.print(":");
  Serial.print(Minute);
  Serial.print(":");
  Serial.print(Second);
  Serial.print(" ");
  Serial.print(BAT_Voltage, 3);
  Serial.print("v ");
  Serial.print(Capacity_f, 1);
  Serial.println("mAh");
}
