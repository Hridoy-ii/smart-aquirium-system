// Blynk Configuration
#define BLYNK_TEMPLATE_ID ""
#define BLYNK_TEMPLATE_NAME ""
#define BLYNK_AUTH_TOKEN ""

//Include necessary libraries
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Pin Definitions
#define TURBIDITY_PIN A0
#define SERVO_PIN 18
#define RED_LED_PIN 19
#define GREEN_LED_PIN 21
#define BUZZER_PIN 22

// System Configuration
#define TURBIDITY_THRESHOLD 500  // Adjust based on your sensor calibration
#define FEEDING_SERVO_ANGLE 90   // Angle to dispense food
#define FEEDING_DURATION 1000    // Duration to hold servo position (ms)

// WiFi and Blynk credentials
char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

// Component initialization
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address might be 0x3F
Servo feedingServo;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 21600); // UTC+6 for Bangladesh

// System variables
int turbidityValue = 0;
int dailyFeedingCount = 0;
bool waterDirtyAlerted = false;
bool systemStarted = false;
unsigned long lastFeedingTime = 0;
unsigned long lastTurbidityCheck = 0;

// Feeding schedule (24-hour format)
int feedingTimes[] = {8, 18}; // 8:00 AM and 6:00 PM
int feedingTimesCount = 2;
bool fedToday[] = {false, false};

// Blynk Virtual Pins
#define V_TURBIDITY V0
#define V_FEEDING_COUNT V1
#define V_MANUAL_FEED V2
#define V_SYSTEM_STATUS V3
#define V_LAST_FEEDING V4

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TURBIDITY_PIN, INPUT);
  
  // Initialize servo
  feedingServo.attach(SERVO_PIN);
  feedingServo.write(0); // Initial position
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Aquarium System");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  
  // Connect to WiFi and Blynk
  Blynk.begin(auth, ssid, pass);
  timeClient.begin();
  
  // System startup indication
  digitalWrite(GREEN_LED_PIN, HIGH);
  systemStarted = true;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready");
  delay(2000);
}

void loop() {
  Blynk.run();
  timeClient.update();
  
  // Check turbidity every 30 seconds
  if (millis() - lastTurbidityCheck > 30000) {
    checkWaterQuality();
    lastTurbidityCheck = millis();
  }
  
  // Check for automatic feeding times
  checkAutomaticFeeding();
  
  // Update display every 5 seconds
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 5000) {
    updateDisplay();
    updateBlynkDashboard();
    lastDisplayUpdate = millis();
  }
  
  delay(100);
}

void checkWaterQuality() {
  turbidityValue = analogRead(TURBIDITY_PIN);
  
  if (turbidityValue > TURBIDITY_THRESHOLD) {
    // Water is dirty
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    delay(500);
    
    if (!waterDirtyAlerted) {
      Blynk.notify("⚠️ Water quality alert! Turbidity level is high. Please check the aquarium.");
      waterDirtyAlerted = true;
    }
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    waterDirtyAlerted = false;
  }
}

void checkAutomaticFeeding() {
  if (!timeClient.isTimeSet()) return;
  
  int currentHour = timeClient.getHours();
  int currentDay = day();
  
  // Reset daily counters at midnight
  static int lastDay = -1;
  if (currentDay != lastDay) {
    resetDailyCounters();
    lastDay = currentDay;
  }
  
  // Check each feeding time
  for (int i = 0; i < feedingTimesCount; i++) {
    if (currentHour == feedingTimes[i] && !fedToday[i]) {
      // Check if we haven't fed in the last hour (prevent multiple feedings)
      if (millis() - lastFeedingTime > 3600000) { // 1 hour
        performFeeding(true); // true for automatic feeding
        fedToday[i] = true;
        
        String message = "🐠 Automatic feeding completed at ";
        message += timeClient.getFormattedTime();
        Blynk.notify(message);
      }
    }
  }
}

void performFeeding(bool isAutomatic) {
  // Move servo to dispense food
  feedingServo.write(FEEDING_SERVO_ANGLE);
  delay(FEEDING_DURATION);
  feedingServo.write(0);
  
  // Update counters
  dailyFeedingCount++;
  lastFeedingTime = millis();
  
  // Display feeding notification
  lcd.clear();
  lcd.setCursor(0, 0);
  if (isAutomatic) {
    lcd.print("Auto Feeding...");
  } else {
    lcd.print("Manual Feeding..");
  }
  lcd.setCursor(0, 1);
  lcd.print("Count: ");
  lcd.print(dailyFeedingCount);
  
  delay(3000);
}

void resetDailyCounters() {
  dailyFeedingCount = 0;
  for (int i = 0; i < feedingTimesCount; i++) {
    fedToday[i] = false;
  }
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Turbidity: ");
  lcd.print(turbidityValue);
  
  lcd.setCursor(0, 1);
  lcd.print("Fed: ");
  lcd.print(dailyFeedingCount);
  lcd.print(" ");
  lcd.print(timeClient.getFormattedTime().substring(0, 5));
}

void updateBlynkDashboard() {
  Blynk.virtualWrite(V_TURBIDITY, turbidityValue);
  Blynk.virtualWrite(V_FEEDING_COUNT, dailyFeedingCount);
  Blynk.virtualWrite(V_SYSTEM_STATUS, systemStarted ? "Online" : "Offline");
  
  if (lastFeedingTime > 0) {
    String lastFeed = "Last fed: " + timeClient.getFormattedTime();
    Blynk.virtualWrite(V_LAST_FEEDING, lastFeed);
  }
}

// Blynk function for manual feeding
BLYNK_WRITE(V_MANUAL_FEED) {
  int buttonState = param.asInt();
  
  if (buttonState == 1) {
    // Check if enough time has passed since last feeding (prevent overfeeding)
    if (millis() - lastFeedingTime > 1800000) { // 30 minutes minimum between feedings
      performFeeding(false); // false for manual feeding
      Blynk.notify("🐠 Manual feeding completed!");
    } else {
      Blynk.notify("⚠️ Please wait before feeding again to prevent overfeeding.");
    }
  }
}

// Blynk connected callback
BLYNK_CONNECTED() {
  Serial.println("Connected to Blynk server");
  Blynk.notify("🟢 Aquarium system is online!");
} 

// Blynk disconnected callback
BLYNK_DISCONNECTED() {
  Serial.println("Disconnected from Blynk server");
}