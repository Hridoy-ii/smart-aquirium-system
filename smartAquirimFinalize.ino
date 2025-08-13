// Blynk Configuration
#define BLYNK_TEMPLATE_ID "TMPL6aLQZDtiH"
#define BLYNK_TEMPLATE_NAME "Smart Aquirium"
#define BLYNK_AUTH_TOKEN "jev6yFiRj7NPTZkFUx73v_Px8OXFZfxq"

// Library included
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <BlynkSimpleEsp32.h>

// WiFi Credentials
char ssid[] = "Redmi Note";
char pass[] = "neverask";

// Pin Definitions
#define TURBIDITY_PIN A0      // GPIO36 (ADC1_CH0)
#define SERVO_PIN 18          // GPIO18 for servo motor
#define RED_LED_PIN 25        // GPIO25 for red LED
#define GREEN_LED_PIN 15      // GPIO15 for green LED
#define BUZZER_PIN 22         // GPIO22 for buzzer
#define SDA_PIN 23            // GPIO23 for I2C SDA
#define SCL_PIN 5             // GPIO5 for I2C SCL

// System Constants
int TURBIDITY_THRESHOLD = 1700;   // Can be changed via Blynk slider
#define FEEDING_HOUR_1 8         // 8:00 AM
#define FEEDING_HOUR_2 18        // 6:00 PM
#define SERVO_FEED_ANGLE 180     // Angle to rotate for feeding
#define SERVO_REST_ANGLE 0       // Servo rest position

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16x2 display

// Servo Configuration
Servo feedingServo;

// Blynk Timer
BlynkTimer timer;

// Global Variables
int turbidityValue = 0;
int waterClarityLevel = 0;  // New variable for 0-100 clarity scale
int dailyFeedCount = 0;
bool fedToday[2] = {false, false};  // Track morning and evening feeding
bool systemStarted = false;
unsigned long lastTurbidityCheck = 0;
unsigned long lastFeedingCheck = 0;
unsigned long redLedBlinkTime = 0;
bool redLedState = false;
int currentDay = 0;

// Non-blocking buzzer variables
unsigned long buzzerTimer = 0;
bool buzzerActive = false;
int buzzerBeepCount = 0;
int buzzerTargetBeeps = 0;
bool buzzerState = false;
unsigned long feedingBuzzerTimer = 0;
bool feedingBuzzerActive = false;
int feedingBuzzerCount = 0;

// Water dirty alert variables
bool waterDirtyAlert = false;
unsigned long dirtyAlertStartTime = 0;

// Blynk connection status
bool blynkConnected = false;

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TURBIDITY_PIN, INPUT);
  
  // Initialize I2C for LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize LCD with proper sequence
  delay(100);
  lcd.init();
  delay(100);
  lcd.backlight();
  delay(100);
  lcd.clear();
  delay(100);
  
  // Initialize servo
  feedingServo.attach(SERVO_PIN);
  feedingServo.write(SERVO_REST_ANGLE);
  
  // Initialize system
  initializeSystem();
  
  // Initialize Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  // Setup Blynk timers
  timer.setInterval(2000L, sendSensorData);    // Send data every 2 seconds
  timer.setInterval(5000L, checkSystemStatus); // Check system every 5 seconds
  timer.setInterval(60000L, checkFeedingTime); // Check feeding every minute
  
  Serial.println("Automated Aquarium System with Blynk Started");
}

void loop() {
  Blynk.run();
  timer.run();
  
  unsigned long currentTime = millis();
  
  // Check turbidity every 5 seconds
  if (currentTime - lastTurbidityCheck >= 5000) {
    checkTurbidity();
    lastTurbidityCheck = currentTime;
  }
  
  // Update display every 2 seconds
  static unsigned long lastDisplayUpdate = 0;
  if (currentTime - lastDisplayUpdate >= 2000) {
    updateDisplay();
    lastDisplayUpdate = currentTime;
  }
  
  // Handle red LED blinking when water is dirty
  if (turbidityValue > TURBIDITY_THRESHOLD) {
    blinkRedLED();
  } else {
    digitalWrite(RED_LED_PIN, LOW);
  }
  
  // Handle non-blocking buzzer for dirty water
  handleDirtyWaterBuzzer();
  
  // Handle non-blocking buzzer for feeding
  handleFeedingBuzzer();
  
  // Reset daily feeding count at midnight
  int today = getCurrentDay();
  if (today != currentDay) {
    currentDay = today;
    dailyFeedCount = 0;
    fedToday[0] = false;
    fedToday[1] = false;
    Blynk.virtualWrite(V3, String(dailyFeedCount) + "/2");
    Serial.println("New day - Reset feeding count");
  }
  
  delay(100);  // Small delay to prevent excessive CPU usage
}

// BLYNK VIRTUAL PIN HANDLERS

// V0 - Water Status LED (Red/Green)
// V1 - Turbidity Value Display
// V2 - Water Clarity Text Display  
// V3 - Daily Feed Count Display
// V4 - Manual Feed Button
BLYNK_WRITE(V4) {
  int buttonState = param.asInt();
  if (buttonState == 1) {
    Serial.println("Manual feeding triggered from Blynk");
    manualFeedFish();
  }
}

// V5 - Turbidity Gauge
// V6 - Feeding Status LED
// V7 - System Online LED
// V8 - Turbidity Threshold Slider
BLYNK_WRITE(V8) {
  TURBIDITY_THRESHOLD = param.asInt();
  Serial.print("Turbidity threshold updated to: ");
  Serial.println(TURBIDITY_THRESHOLD);
  Blynk.virtualWrite(V9, "Threshold: " + String(TURBIDITY_THRESHOLD));
}

// V9 - Next Feed Time / Threshold Display
// V10 - Reset Feed Counter Button
BLYNK_WRITE(V10) {
  int buttonState = param.asInt();
  if (buttonState == 1) {
    dailyFeedCount = 0;
    fedToday[0] = false;
    fedToday[1] = false;
    Blynk.virtualWrite(V3, String(dailyFeedCount) + "/2");
    Blynk.logEvent("feed_reset", "Daily feed counter has been reset manually");
    Serial.println("Feed counter reset from Blynk");
  }
}

// V11 - Water Clarity Level (0-100 scale)
// This is a new virtual pin for the clarity percentage

// Blynk connection status
BLYNK_CONNECTED() {
  blynkConnected = true;
  Serial.println("Blynk Connected");
  
  // Initialize Blynk widgets with current values
  updateBlynkWidgets();
  
  // Sync all virtual pins
  Blynk.syncAll();
}

BLYNK_DISCONNECTED() {
  blynkConnected = false;
  Serial.println("Blynk Disconnected");
}

void initializeSystem() {
  // Turn on green LED to indicate system is powered on
  digitalWrite(GREEN_LED_PIN, HIGH);
  
  // Multiple LCD initialization attempts to prevent garbage
  for(int attempt = 0; attempt < 3; attempt++) {
    lcd.init();
    delay(100);
  }
  lcd.backlight();
  lcd.clear();
  delay(500);
  
  // Display startup message
  lcd.setCursor(0, 0);
  lcd.print("Aquarium System ");
  lcd.setCursor(0, 1);
  lcd.print("Starting...     ");
  
  // Short startup beep
  startFeedingBuzzer(1);
  
  delay(3000);
  
  // Show ready message
  lcd.clear();
  delay(100);
  lcd.setCursor(0, 0);
  lcd.print("System Ready!   ");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi.");
  
  delay(2000);
  
  // Get current day for daily reset
  currentDay = getCurrentDay();
  
  systemStarted = true;
  Serial.println("System initialization complete");
}

void checkTurbidity() {
  turbidityValue = analogRead(TURBIDITY_PIN);
  
  // Calculate water clarity level (0-100 scale)
  // Higher clarity means cleaner water
  waterClarityLevel = calculateWaterClarity(turbidityValue);
  
  // Convert ADC reading to voltage (ESP32 ADC is 12-bit, 0-4095)
  float voltage = turbidityValue * (3.3 / 4095.0);
  
  Serial.print("Turbidity Value: ");
  Serial.print(turbidityValue);
  Serial.print(" (");
  Serial.print(voltage);
  Serial.print("V) - Clarity Level: ");
  Serial.print(waterClarityLevel);
  Serial.println("%");
  
  // Check if water is dirty
  static bool wasDirty = false;
  bool isDirty = (turbidityValue > TURBIDITY_THRESHOLD);
  
  if (isDirty && !wasDirty) {
    // Water just became dirty
    Serial.println("Water is dirty! Activating alert...");
    waterDirtyAlert = true;
    dirtyAlertStartTime = millis();
    startDirtyWaterBuzzer(3);
    
    // Send Blynk notification
    if (blynkConnected) {
      Blynk.logEvent("water_dirty", "Water quality alert! Turbidity: " + String(turbidityValue) + " Clarity: " + String(waterClarityLevel) + "%");
    }
  }
  
  // Clear dirty alert if water becomes clean
  if (!isDirty && wasDirty) {
    waterDirtyAlert = false;
    Serial.println("Water is now clean");
    
    // Send Blynk notification
    if (blynkConnected) {
      Blynk.logEvent("water_clean", "Water quality improved! Turbidity: " + String(turbidityValue) + " Clarity: " + String(waterClarityLevel) + "%");
    }
  }
  
  wasDirty = isDirty;
}

// New function to calculate water clarity level (0-100 scale)
int calculateWaterClarity(int turbidity) {
  // Calculate 80% of threshold (crystal clear threshold)
  int crystalClearThreshold = TURBIDITY_THRESHOLD * 0.2;  // 20% of threshold
  
  // If turbidity is very low (20% or less of threshold), return 100% clarity
  if (turbidity <= crystalClearThreshold) {
    return 100;
  }
  
  // If turbidity exceeds threshold, return 0% clarity
  if (turbidity >= TURBIDITY_THRESHOLD) {
    return 0;
  }
  
  // Linear interpolation between crystal clear threshold and dirty threshold
  // Higher turbidity = lower clarity
  int clarityRange = TURBIDITY_THRESHOLD - crystalClearThreshold;
  int turbidityFromClear = turbidity - crystalClearThreshold;
  
  int clarity = 100 - ((turbidityFromClear * 100) / clarityRange);
  
  // Ensure clarity is within 0-100 range
  clarity = constrain(clarity, 0, 100);
  
  return clarity;
}

void checkFeedingTime() {
  int currentHour = getCurrentHour();
  
  // Morning feeding (8:00 AM)
  if (currentHour == FEEDING_HOUR_1 && !fedToday[0]) {
    Serial.println("Morning feeding time!");
    feedFish();
    fedToday[0] = true;
    dailyFeedCount++;
    
    // Update Blynk
    if (blynkConnected) {
      Blynk.virtualWrite(V3, String(dailyFeedCount) + "/2");
      Blynk.logEvent("feeding", "Morning feeding completed automatically");
    }
  }
  
  // Evening feeding (6:00 PM)
  if (currentHour == FEEDING_HOUR_2 && !fedToday[1]) {
    Serial.println("Evening feeding time!");
    feedFish();
    fedToday[1] = true;
    dailyFeedCount++;
    
    // Update Blynk
    if (blynkConnected) {
      Blynk.virtualWrite(V3, String(dailyFeedCount) + "/2");
      Blynk.logEvent("feeding", "Evening feeding completed automatically");
    }
  }
}

void feedFish() {
  Serial.println("Feeding fish...");
  
  // Show feeding message on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FEEDING TIME!   ");
  lcd.setCursor(0, 1);
  lcd.print("Dispensing food ");
  
  // Update Blynk feeding status
  if (blynkConnected) {
    Blynk.virtualWrite(V6, 255); // Turn on feeding LED
  }
  
  // Short double beep for feeding time
  startFeedingBuzzer(2);
  
  // Rotate servo to dispense food
  feedingServo.write(SERVO_FEED_ANGLE);
  delay(1000);  // Keep servo in feeding position for 1 second
  
  // Return servo to rest position
  feedingServo.write(SERVO_REST_ANGLE);
  delay(500);
  
  // Show completion message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Feed Complete!  ");
  lcd.setCursor(0, 1);
  lcd.print("Count: ");
  lcd.print(dailyFeedCount);
  lcd.print("/2      ");
  
  delay(2000);  // Show message for 2 seconds
  
  // Turn off feeding LED
  if (blynkConnected) {
    Blynk.virtualWrite(V6, 0);
  }
  
  Serial.println("Feeding complete");
}

void manualFeedFish() {
  // Prevent overfeeding
  if (dailyFeedCount >= 2) {
    if (blynkConnected) {
      Blynk.logEvent("feed_limit", "Daily feeding limit reached (2/2)");
    }
    Serial.println("Daily feeding limit reached");
    return;
  }
  
  feedFish();
  dailyFeedCount++;
  
  // Update Blynk
  if (blynkConnected) {
    Blynk.virtualWrite(V3, String(dailyFeedCount) + "/2");
    Blynk.logEvent("manual_feed", "Manual feeding completed via app");
  }
}

void updateDisplay() {
  // Clear display to prevent garbage
  lcd.clear();
  delay(50);  // Small delay for LCD stability
  
  // Line 1: Water clarity text and clarity percentage
  lcd.setCursor(0, 0);
  String clarity = getClarityLevel();
  lcd.print(clarity);
  
  // Add clarity percentage to first line if there's space
  int clarityTextLen = clarity.length();
  if (clarityTextLen <= 12) {  // Leave space for percentage
    int spaces = 16 - clarityTextLen - 4;  // Reserve 4 chars for "XX%"
    for(int i = 0; i < spaces; i++) {
      lcd.print(" ");
    }
    lcd.print(waterClarityLevel);
    lcd.print("%");
  }
  
  // Line 2: Show appropriate message
  lcd.setCursor(0, 1);
  
  if (waterDirtyAlert && turbidityValue > TURBIDITY_THRESHOLD) {
    // Show "Water got Dirty" message when water is dirty
    lcd.print("Water got Dirty!");
  } else {
    // Normal display: Turbidity value, feeding count, and connection status
    lcd.print("T:");
    lcd.print(turbidityValue);
    lcd.print(" F:");
    lcd.print(dailyFeedCount);
    lcd.print("/2");
    
    if (blynkConnected) {
      lcd.print(" W:ON");
    } else {
      lcd.print(" W:OFF");
    }
  }
}

// Blynk data sender function
void sendSensorData() {
  if (blynkConnected) {
    // Update all Blynk widgets
    updateBlynkWidgets();
  }
}

void updateBlynkWidgets() {
  // V0 - Water Status LED
  if (turbidityValue > TURBIDITY_THRESHOLD) {
    Blynk.virtualWrite(V0, 255); // Red for dirty water
  } else {
    Blynk.virtualWrite(V0, 0);   // Off for clean water
  }
  
  // V1 - Turbidity Value
  Blynk.virtualWrite(V1, turbidityValue);
  
  // V2 - Water Clarity Text
  Blynk.virtualWrite(V2, getClarityLevel());
  
  // V3 - Daily Feed Count
  Blynk.virtualWrite(V3, String(dailyFeedCount) + "/2");
  
  // V5 - Water Clarity Gauge (0-100 range)
  Blynk.virtualWrite(V5, waterClarityLevel);
  
  // V7 - System Online LED
  Blynk.virtualWrite(V7, 255); // Always on when connected
  
  // V9 - Next Feed Time
  int currentHour = getCurrentHour();
  int nextFeed;
  String nextFeedText;
  
  if (currentHour < FEEDING_HOUR_1) {
    nextFeed = FEEDING_HOUR_1;
    nextFeedText = "Next: " + String(nextFeed) + ":00 AM";
  } else if (currentHour < FEEDING_HOUR_2) {
    nextFeed = FEEDING_HOUR_2;
    nextFeedText = "Next: " + String(nextFeed) + ":00 PM";
  } else {
    nextFeedText = "Next: 8:00 AM (Tomorrow)";
  }
  
  Blynk.virtualWrite(V9, nextFeedText);
  
  // V11 - Water Clarity Level (0-100 scale) - Same as V5 but as separate widget
  Blynk.virtualWrite(V11, waterClarityLevel);
}

void checkSystemStatus() {
  // This function runs every 5 seconds to check system health
  if (blynkConnected) {
    // Update system status
    Blynk.virtualWrite(V7, 255); // System online
  }
}

// Updated getClarityLevel function based on threshold percentage
String getClarityLevel() {
  // Calculate percentage thresholds based on TURBIDITY_THRESHOLD
  int crystalClearMax = TURBIDITY_THRESHOLD * 0.4;   // 20% of threshold
  int clearMax = TURBIDITY_THRESHOLD * 0.5;          // 40% of threshold
  int slightlyCloudyMax = TURBIDITY_THRESHOLD * 0.6; // 60% of threshold
  int cloudyMax = TURBIDITY_THRESHOLD * 0.7;         // 80% of threshold
  int veryCloudyMax = TURBIDITY_THRESHOLD * 0.8;    // 95% of threshold
  
  if (turbidityValue <= crystalClearMax) {
    return "Crystal Clear";
  } else if (turbidityValue <= clearMax) {
    return "Clear";
  } else if (turbidityValue <= slightlyCloudyMax) {
    return "Slightly Cloudy";
  } else if (turbidityValue <= cloudyMax) {
    return "Cloudy";
  } else if (turbidityValue <= veryCloudyMax) {
    return "Very Cloudy";
  } else {
    return "Dirty Water";
  }
}

void blinkRedLED() {
  unsigned long currentTime = millis();
  
  if (currentTime - redLedBlinkTime >= 500) {  // Blink every 500ms
    redLedState = !redLedState;
    digitalWrite(RED_LED_PIN, redLedState);
    redLedBlinkTime = currentTime;
  }
}

// Non-blocking buzzer functions
void startDirtyWaterBuzzer(int beeps) {
  buzzerActive = true;
  buzzerBeepCount = 0;
  buzzerTargetBeeps = beeps;
  buzzerTimer = millis();
  buzzerState = false;
  digitalWrite(BUZZER_PIN, LOW);
}

void startFeedingBuzzer(int beeps) {
  feedingBuzzerActive = true;
  feedingBuzzerCount = 0;
  feedingBuzzerTimer = millis();
  buzzerTargetBeeps = beeps * 2; // Double for on/off cycles
}

void handleDirtyWaterBuzzer() {
  if (!buzzerActive) return;
  
  unsigned long currentTime = millis();
  
  if (currentTime - buzzerTimer >= 500) { // Long beeps for dirty water
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState);
    buzzerTimer = currentTime;
    
    if (!buzzerState) { // Just turned off
      buzzerBeepCount++;
      if (buzzerBeepCount >= buzzerTargetBeeps) {
        buzzerActive = false;
        digitalWrite(BUZZER_PIN, LOW);
      }
    }
  }
}

void handleFeedingBuzzer() {
  if (!feedingBuzzerActive) return;
  
  unsigned long currentTime = millis();
  
  if (currentTime - feedingBuzzerTimer >= 200) { // Short beeps for feeding
    if (feedingBuzzerCount < buzzerTargetBeeps) {
      digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
      feedingBuzzerCount++;
      feedingBuzzerTimer = currentTime;
    } else {
      feedingBuzzerActive = false;
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

// Simulate getting current hour (0-23)
// In a real implementation, you might use RTC or NTP
int getCurrentHour() {
  // For simulation, use millis() to cycle through hours
  return (millis() / 60000) % 24;  // Each minute represents an hour
}

// Simulate getting current day
int getCurrentDay() {
  // For simulation, increment day every 24 "hours"
  return (millis() / (60000 * 24));  // New day every 24 minutes
}

/*
UPDATED BLYNK SETUP GUIDE:
==========================

1. WIDGET CONFIGURATION (2000/2000 energy used):
   
   V0 - LED Widget (100 energy)
   - Name: "Water Status"
   - Color: Red for dirty, Off for clean
   
   V1 - Value Display (0 energy)
   - Name: "Turbidity Raw Value"
   - Units: "ADC"
   
   V2 - Value Display (0 energy)
   - Name: "Water Clarity Text"
   - Reading Frequency: 2 sec
   
   V3 - Value Display (0 energy)
   - Name: "Daily Feeds"
   - Shows: "X/2"
   
   V4 - Button (200 energy)
   - Name: "Manual Feed"
   - Mode: Push
   - Color: Blue
   
   V5 - Gauge (200 energy)
   - Name: "Water Clarity Level"
   - Min: 0, Max: 100
   - Units: "%"
   - Color: Red to Green gradient (0% red, 100% green)
   
   V6 - LED Widget (100 energy)
   - Name: "Feeding Status"
   - Color: Yellow (on during feeding)
   
   V7 - LED Widget (100 energy)
   - Name: "System Online"
   - Color: Green
   
   V8 - Slider (200 energy)
   - Name: "Turbidity Threshold"
   - Min: 100, Max: 1000
   - Default: 500
   
   V9 - Value Display (0 energy)
   - Name: "Next Feeding"
   - Shows feeding schedule
   
   V10 - Button (200 energy)
   - Name: "Reset Counter"
   - Mode: Push
   - Color: Orange
   
   V11 - Gauge (200 energy) [NEW]
   - Name: "Water Clarity Level"
   - Min: 0, Max: 100
   - Units: "%"
   - Color: Red to Green gradient

2. KEY CHANGES MADE:
   - Added waterClarityLevel variable (0-100 scale)
   - Added calculateWaterClarity() function
   - Updated getClarityLevel() to use threshold-based percentages
   - Modified updateDisplay() to show clarity percentage
   - Added V11 for water clarity gauge
   - Updated notifications to include clarity percentage

3. CLARITY CALCULATION:
   - Crystal Clear: 0-20% of threshold (100% clarity)
   - Clear: 20-40% of threshold (80-100% clarity)
   - Slightly Cloudy: 40-60% of threshold (60-80% clarity)
   - Cloudy: 60-80% of threshold (40-60% clarity)
   - Very Cloudy: 80-95% of threshold (20-40% clarity)
   - Dirty Water: Above 95% of threshold (0-20% clarity)

4. MODERN UI LAYOUT SUGGESTION:
   Row 1: V0 (Water Status LED) | V5 (Clarity % Gauge) | V1 (Turbidity Raw Value)
   Row 2: V2 (Clarity Text) | V11 (Clarity % Gauge 2) | V7 (System Online)
   Row 3: V3 (Feed Count) | V4 (Manual Feed Button) | V6 (Feeding LED)
   Row 4: V8 (Threshold Slider) | V9 (Next Feed) | V10 (Reset Button)

Now you have:
- Separate turbidity raw value and water clarity percentage
- Dynamic thresholds based on your configurable TURBIDITY_THRESHOLD
- Higher clarity percentage means cleaner water
- Threshold-relative clarity levels instead of fixed values
*/