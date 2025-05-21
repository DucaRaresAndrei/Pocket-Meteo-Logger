/*
Duca Andrei-Rares
331CA
*/

// Required libraries for the sensor, OLED display, SD card, WiFi connection and HTTP communication
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FS.h"
#include "SD.h"
#include <WiFi.h>
#include <HTTPClient.h>

// OLED screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Atmospheric pressure at sea level (used for altitude calculation)
#define SEALEVELPRESSURE_HPA (1013.25)

// Initialize sensor and display objects
Adafruit_BME680 bme;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin definitions for LEDs, buzzer and microSD
#define LED_GREEN 27
#define LED_RED 26
#define BUZZER_PIN 25
#define SD_CS 5

// Pin definitions for buttons
#define BUTTON_NEXT 14
#define BUTTON_PREV 13
#define BUTTON_ENTER 12

// Threshold values for normal sensor readings
#define TEMP_MIN 15.0
#define TEMP_MAX 30.0
#define PRESSURE_MIN 950.0
#define PRESSURE_MAX 1050.0
#define HUMIDITY_MIN 30.0
#define HUMIDITY_MAX 60.0
#define GAS_MIN 0.0
#define GAS_MAX 50.0

// WiFi configuration and Google Sheets URL
const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";
const char* googleScriptURL = "your/link/to/script";

// Google Sheet page:
// https://docs.google.com/spreadsheets/d/1Z4F9pUHt_T0Ys5WkMET9dgWCy-Dyrn1qGVPz2djs_G4/edit?gid=0#gid=0

// Time and state variables
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 30000; // 30 secunde
unsigned long buzzerStartTime = 0;
bool buzzerActive = false;

// Current mode (live or history)
bool inHistoryMode = false;
int historyIndex = 0;
String historyLines[1000]; // History lines read from SD file
int totalHistoryLines = 0;

// WiFi connection status tracking
bool wifiConnected = false;
unsigned long lastWiFiAttempt = 0;
const unsigned long wifiRetryInterval = 240000; // Incearca o data la 4 minute

// Initialize SD card and display its details
void initSDCard() {
  if (!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    while (1);
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    while (1);
  }

  // Display SD card type and size
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

// Append a new line of sensor data to the SD file
void appendToSD(String data) {
  File file = SD.open("/data.txt", FILE_APPEND);

  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }

  if (file.print(data)) {
    Serial.println("Data appended to file");
  } else {
    Serial.println("Append failed");
  }

  file.close();
}

// Send the measured data to Google Sheets via HTTP POST
void sendDataToGoogleSheets(float temp, float press, float hum, float gas, float alt) {
  if (!wifiConnected)
    return;

  HTTPClient http;
  http.begin(googleScriptURL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"temperature\":" + String(temp) +
                    ",\"pressure\":" + String(press) +
                    ",\"humidity\":" + String(hum) +
                    ",\"gas\":" + String(gas) +
                    ",\"altitude\":" + String(alt) + "}";

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Google Sheets Response: " + response);
  } else {
    Serial.println("Error sending data to Google Sheets");
  }

  http.end();
}

// Try connecting to WiFi if disconnected
void tryConnectWiFi() {
  if (lastWiFiAttempt != 0)
    if (wifiConnected || (millis() - lastWiFiAttempt < wifiRetryInterval))
      return;

  lastWiFiAttempt = millis(); // update timestamp incercare
  Serial.println("Incercare reconectare WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting");

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.print("\nConectat la WiFi, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi indisponibil.");
  }
}

// Display a specific line from the SD history on the OLED
void displayHistoryLine(int index) {
  if (index < 0 || index >= totalHistoryLines) return;

  // Parse CSV line into separate values
  String line = historyLines[index];
  float values[6];
  int valueIndex = 0;
  int lastComma = -1;
  
  for (int i = 0; i < line.length(); i++) {
    if (line[i] == ',' || i == line.length() - 1) {
      String part;
      
      if (i == line.length() - 1) {
        part = line.substring(lastComma + 1);
      } else {
        part = line.substring(lastComma + 1, i);
      }

      part.trim();
      
      values[valueIndex++] = part.toFloat();
      lastComma = i;
    }
  }

  // Display the parsed values on OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("BME680 Readings:");
  display.printf("Temp: %.2f *C\n", values[1]);
  display.printf("Press: %.2f hPa\n", values[2]);
  display.printf("Hum: %.2f %%\n", values[3]);
  display.printf("Gas: %.2f KOhms\n", values[4]);
  display.printf("Alt: %.2f m\n", values[5]);
  display.display();
}

// Load all history lines from SD into memory
void loadHistoryFromSD() {
  File file = SD.open("/data.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open data.txt for reading");
    return;
  }

  totalHistoryLines = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 1) {
      historyLines[totalHistoryLines++] = line;
    }
  }

  file.close();
  Serial.println("History loaded successfully");
}

// Main setup function
void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Try to connect to WiFi
  tryConnectWiFi();

  // Initialize the BME680 sensor
  if (!bme.begin()) {
    Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
    while (1);
  }

  // Sensor configuration
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320°C for 150 ms

  // Initialize the OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Pocket Meteo Logger");
  display.display();
  delay(2000);

  // Configure LEDs and buzzer
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerActive = false;

  // Initialize the SD card
  initSDCard();

  // Create the data file if it doesn't already exist
  File file = SD.open("/data.txt");
  if (!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    appendToSD("Timestamp, Temperature, Pressure, Humidity, Gas, Altitude\n");
  }
  file.close();

  // Configure buttons as INPUT_PULLUP
  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PREV, INPUT_PULLUP);
  pinMode(BUTTON_ENTER, INPUT_PULLUP);

  // Load historical data from SD card
  loadHistoryFromSD();
}

// Main loop function
void loop() {
  // Attempt to reconnect to WiFi if disconnected
  tryConnectWiFi();

   // Check if we need to switch between live mode and history mode
  if (digitalRead(BUTTON_ENTER) == LOW) {
      delay(200); // Debounce
      
      if (inHistoryMode) {
          // Exit history mode
          inHistoryMode = false;
          display.clearDisplay();
          display.setCursor(0, 0);
          display.setTextSize(1);
          display.setTextColor(WHITE);
          display.println("Returning to live mode...");
          display.display();
          delay(500);  // Short delay to show the message
      } else {
          // Enter history mode
          inHistoryMode = true;
          historyIndex = 0;  // Start from the first line
          displayHistoryLine(historyIndex);
          digitalWrite(LED_GREEN, LOW);
          digitalWrite(LED_RED, LOW);
          digitalWrite(BUZZER_PIN, HIGH);

          display.clearDisplay();
          display.setCursor(0, 0);
          display.setTextSize(1);
          display.setTextColor(WHITE);
          display.println("History Mode:");
          display.display();
          delay(500);  // Short delay to show the message
      }

      // Final debounce to prevent rapid toggling
      delay(500);
      return;
  }

  // If in history mode, navigate through history entries
  if (inHistoryMode) {
    if (digitalRead(BUTTON_NEXT) == LOW) {
      delay(200);  // Debounce
      historyIndex++;
      if (historyIndex >= totalHistoryLines) historyIndex = totalHistoryLines - 1;
      displayHistoryLine(historyIndex);
      return;
    }

    if (digitalRead(BUTTON_PREV) == LOW) {
      delay(200);  // Debounce
      historyIndex--;
      if (historyIndex < 0) historyIndex = 0;
      displayHistoryLine(historyIndex);
      return;
    }
  }

  // If in live mode, read sensor and display data on screen
  if (!inHistoryMode) {
    unsigned long currentTime = millis();

    // Check if 30 seconds have passed since last update
    if (currentTime - lastUpdateTime >= updateInterval) {
      lastUpdateTime = currentTime;

      // Perform a reading from the BME680 sensor
      if (!bme.performReading()) {
        Serial.println(F("Failed to complete reading :("));
        return;
      }

      // Read sensor values
      float temperature = bme.temperature;
      float pressure = bme.pressure / 100.0;
      float humidity = bme.humidity;
      float gasResistance = bme.gas_resistance / 1000.0;
      float altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
      unsigned long timestamp = millis() / 1000;

      // Output to serial
      Serial.printf("Temp: %.2f *C, Press: %.2f hPa, Hum: %.2f %%, Gas: %.2f KOhms, Alt: %.2f m\n",
                    temperature, pressure, humidity, gasResistance, altitude);

      // Send data to Google Sheets
      sendDataToGoogleSheets(temperature, pressure, humidity, gasResistance, altitude);

      // Display data on OLED
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("BME680 Readings:");
      display.printf("Temp: %.2f *C\n", temperature);
      display.printf("Press: %.2f hPa\n", pressure);
      display.printf("Hum: %.2f %%\n", humidity);
      display.printf("Gas: %.2f KOhms\n", gasResistance);
      display.printf("Alt: %.2f m\n", altitude);
      display.display();

      // Save data to SD card
      String data = String(timestamp) + " , " +
                    String(temperature) + " , " +
                    String(pressure) + " , " +
                    String(humidity) + " , " +
                    String(gasResistance) + " , " +
                    String(altitude) + "\n";
      appendToSD(data);

      // Check thresholds and activate appropriate indicators
      if ((temperature >= TEMP_MIN && temperature <= TEMP_MAX) &&
          (pressure >= PRESSURE_MIN && pressure <= PRESSURE_MAX) &&
          (humidity >= HUMIDITY_MIN && humidity <= HUMIDITY_MAX) &&
          (gasResistance >= GAS_MIN && gasResistance <= GAS_MAX)) {
        // Normal values
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, LOW);
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerActive = false;
      } else {
        // Alert: values out of normal range
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);

        // Activate buzzer for 4 seconds
        if (!buzzerActive) {
          buzzerStartTime = millis();
          buzzerActive = true;
          digitalWrite(BUZZER_PIN, LOW);
        }
      }
    }

    // Turn off buzzer after 4 seconds
    if (buzzerActive && millis() - buzzerStartTime >= 4000) {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerActive = false;
    }
  }
}
