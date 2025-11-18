/**
 * E-Ink Family Calendar with ESP32
 *
 * Features:
 * - WiFi connectivity with auto-reconnect
 * - E-ink display (Waveshare 7.5" 800x600)
 * - Google Calendar integration
 * - Weather information
 * - Deep sleep for battery efficiency
 */

#include <GxEPD2_3C.h>  // 3-color (red/black/white) displays
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <time.h>

#include "config.h"

// ==================== SimpleWiFi - Inline Implementation ====================
class SimpleWiFi {
private:
    const char* ssid;
    const char* password;

public:
    SimpleWiFi(const char* ssid, const char* password) {
        this->ssid = ssid;
        this->password = password;
    }

    bool connect(int timeout_seconds = 30) {
        Serial.print("[WiFi] Connecting to: ");
        Serial.println(ssid);

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < timeout_seconds * 2) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WiFi] Connected!");
            Serial.print("[WiFi] IP: ");
            Serial.println(WiFi.localIP());
            Serial.print("[WiFi] RSSI: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            return true;
        }

        Serial.println("\n[WiFi] Connection timeout!");
        return false;
    }

    void disconnect() {
        WiFi.disconnect();
        Serial.println("[WiFi] Disconnected");
    }

    int getRSSI() {
        return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    }
};

// ==================== SimpleAWSIoT - Inline Implementation ====================
class SimpleAWSIoT {
private:
    WiFiClientSecure wifiClient;
    PubSubClient mqttClient;
    String endpoint;
    String thingName;
    String clientID;
    bool certificatesLoaded;

    bool loadCertificateFile(const String& path, String& content) {
        File file = LittleFS.open(path, "r");
        if (!file) {
            Serial.print("[AWS] Failed to open: ");
            Serial.println(path);
            return false;
        }

        content = file.readString();
        file.close();

        if (content.length() == 0) {
            Serial.print("[AWS] Empty file: ");
            Serial.println(path);
            return false;
        }

        Serial.print("[AWS] Loaded: ");
        Serial.print(path);
        Serial.print(" (");
        Serial.print(content.length());
        Serial.println(" bytes)");
        return true;
    }

    bool syncTime() {
        Serial.print("[AWS] Syncing time...");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        int retries = 0;
        while (retries < 10) {
            time_t now = time(nullptr);
            if (now > 24 * 3600) {
                Serial.println(" ✓");
                return true;
            }
            delay(1000);
            Serial.print(".");
            retries++;
        }

        Serial.println(" ✗ Failed!");
        return false;
    }

public:
    SimpleAWSIoT(const String& endpoint, const String& thingName, const String& clientId = "") {
        this->endpoint = endpoint;
        this->thingName = thingName;
        this->clientID = clientId.isEmpty() ? thingName : clientId;
        this->certificatesLoaded = false;

        mqttClient.setClient(wifiClient);
        mqttClient.setServer(endpoint.c_str(), 8883);
        mqttClient.setBufferSize(2048);
    }

    bool loadCertificates() {
        Serial.println("[AWS] Loading certificates...");

        if (!LittleFS.begin(true)) {
            Serial.println("[AWS] LittleFS mount failed!");
            return false;
        }

        String rootCA, deviceCert, privateKey;

        if (!loadCertificateFile("/certs/root-ca.pem", rootCA) ||
            !loadCertificateFile("/certs/certificate.pem.crt", deviceCert) ||
            !loadCertificateFile("/certs/private.pem.key", privateKey)) {
            Serial.println("[AWS] Certificate loading failed!");
            return false;
        }

        wifiClient.setCACert(rootCA.c_str());
        wifiClient.setCertificate(deviceCert.c_str());
        wifiClient.setPrivateKey(privateKey.c_str());

        certificatesLoaded = true;
        Serial.println("[AWS] Certificates loaded!");
        return true;
    }

    bool connect() {
        if (!certificatesLoaded) {
            Serial.println("[AWS] No certificates loaded!");
            return false;
        }

        if (!syncTime()) {
            Serial.println("[AWS] Time sync failed!");
            return false;
        }

        Serial.print("[AWS] Connecting to: ");
        Serial.println(endpoint);

        bool connected = mqttClient.connect(clientID.c_str(), NULL, NULL, NULL, 0, false, NULL, true);

        if (connected) {
            Serial.println("[AWS] Connected!");
            return true;
        }

        Serial.print("[AWS] Connection failed, rc=");
        Serial.println(mqttClient.state());
        return false;
    }

    void disconnect() {
        mqttClient.disconnect();
        Serial.println("[AWS] Disconnected");
    }

    void setCallback(MQTT_CALLBACK_SIGNATURE) {
        mqttClient.setCallback(callback);
    }

    bool subscribe(const String& topic, int qos = 0) {
        bool result = mqttClient.subscribe(topic.c_str(), qos);
        if (result) {
            Serial.print("[AWS] Subscribed: ");
            Serial.println(topic);
        }
        return result;
    }

    bool publish(const String& topic, const String& payload) {
        bool result = mqttClient.publish(topic.c_str(), payload.c_str());
        if (result) {
            Serial.print("[AWS] Published to ");
            Serial.println(topic);
        }
        return result;
    }

    void loop() {
        mqttClient.loop();
    }

    unsigned long getTimestamp() {
        return time(nullptr);
    }
};

// ==================== End of Inline Libraries ====================

// Display configuration for Waveshare 7.5" (800x480) 3-color
// Pin mapping for LOLIN32 with 9-pin HAT connector (verified working)
// HAT Pin   -> LOLIN32
// VCC       -> 3.3V (DO NOT USE 5V - will damage ESP32!)
// GND       -> GND
// DIN       -> GPIO 23 (MOSI)
// CLK       -> GPIO 18 (SCK)
// CS        -> GPIO 5
// DC        -> GPIO 17
// RST       -> GPIO 16
// BUSY      -> GPIO 4
// PWR       -> GPIO 2 (HAT power control - set HIGH to enable)

#define EPD_CS      5
#define EPD_DC      17
#define EPD_RST     16
#define EPD_BUSY    4
#define EPD_PWR     2   // HAT power control pin
// Hardware SPI pins for LOLIN32: SCK=18, MOSI=23

// Initialize display - 7.5" 3-color (red/black/white) 800x480
// Model: 075RW-Z08 (GDEW075Z08 variant)
// Using paged rendering (height/2) to fit in ESP32 RAM
GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / 2> display(GxEPD2_750c_Z08(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// WiFi manager
SimpleWiFi wifi(WIFI_SSID, WIFI_PASSWORD);

// AWS IoT client - create unique client ID to avoid DUPLICATE_CLIENT_ID errors on reset
// Use thing name + random suffix for uniqueness
String uniqueClientID = String(AWS_THING_NAME) + "-" + String(esp_random(), HEX);
SimpleAWSIoT awsIot(AWS_IOT_ENDPOINT, AWS_THING_NAME, uniqueClientID);

// Global variables for MQTT data
String calendarData = "";
String weatherData = "";
bool calendarReceived = false;
bool weatherReceived = false;

// Display dimensions (rotated 90 degrees for portrait)
const int DISPLAY_WIDTH = 480;   // Width after rotation
const int DISPLAY_HEIGHT = 800;  // Height after rotation

// Layout constants - Vertical stacking (compact for display case)
const int HEADER_HEIGHT = 203;        // Header area for weather/date (moved down 2.5cm)
const int EVENT_START_Y = 260;        // Start below weather section (moved down 1.5cm)
const int EVENT_HEIGHT = 85;          // Height for each event (compact)
const int MAX_EVENTS = 6;             // 6 events
const int WEATHER_X = 43;             // Weather position (0.5cm from left edge)
const int WEATHER_Y = 118;            // Weather at top (moved down 2.5cm = ~98px)

// Sleep time (from config.h)

// Helper function to shorten Finnish day names
String shortenDate(String date) {
  // Shorten Finnish weekday names (first word) - handle both cases
  date.replace("Maanantai", "Ma");
  date.replace("maanantai", "Ma");
  date.replace("Tiistai", "Ti");
  date.replace("tiistai", "Ti");
  date.replace("Keskiviikko", "Ke");
  date.replace("keskiviikko", "Ke");
  date.replace("Torstai", "To");
  date.replace("torstai", "To");
  date.replace("Perjantai", "Pe");
  date.replace("perjantai", "Pe");
  date.replace("Lauantai", "La");
  date.replace("lauantai", "La");
  date.replace("Sunnuntai", "Su");
  date.replace("sunnuntai", "Su");

  return date;
}

// Helper function to replace Finnish/special characters with ASCII equivalents
String replaceSpecialChars(String text) {
  // UTF-8 replacements for Finnish characters
  text.replace("Ä", "A");
  text.replace("ä", "a");
  text.replace("Ö", "O");
  text.replace("ö", "o");
  text.replace("Å", "A");
  text.replace("å", "a");

  // UTF-8 hex codes (in case they come as raw bytes)
  text.replace("\xC3\x84", "A");  // Ä
  text.replace("\xC3\xA4", "a");  // ä
  text.replace("\xC3\x96", "O");  // Ö
  text.replace("\xC3\xB6", "o");  // ö
  text.replace("\xC3\x85", "A");  // Å
  text.replace("\xC3\xA5", "a");  // å

  return text;
}

// Weather icon drawing primitives (inspired by G6EJD ESP32-e-Paper-Weather-Display)
// Now with color support
void addcloud(int x, int y, int scale, int linesize, uint16_t color = GxEPD_BLACK) {
  // Draw cloud using circles and rectangles
  display.fillCircle(x - scale * 3, y, scale, color);
  display.fillCircle(x + scale * 3, y, scale, color);
  display.fillCircle(x - scale, y - scale, scale * 1.4, color);
  display.fillCircle(x + scale, y - scale, scale * 1.4, color);
  display.fillRect(x - scale * 3 - 1, y - scale, scale * 6, scale * 2 + 1, color);

  // Clear interior with white to create outline
  display.fillCircle(x - scale * 3, y, scale - linesize, GxEPD_WHITE);
  display.fillCircle(x + scale * 3, y, scale - linesize, GxEPD_WHITE);
  display.fillCircle(x - scale, y - scale, scale * 1.4 - linesize, GxEPD_WHITE);
  display.fillCircle(x + scale, y - scale, scale * 1.4 - linesize, GxEPD_WHITE);
  display.fillRect(x - scale * 3 + 2, y - scale + linesize - 1, scale * 5.9, scale * 2 - linesize * 2 + 2, GxEPD_WHITE);
}

void addsun(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  int linesize = 2;
  int dxo, dyo, dxi, dyi;

  // Draw sun rays
  display.fillCircle(x, y, scale, color);
  display.fillCircle(x, y, scale - linesize, GxEPD_WHITE);

  for (float i = 0; i < 360; i = i + 45) {
    dxo = 2.2 * scale * cos((i - 90) * 3.14 / 180); dxi = dxo * 0.6;
    dyo = 2.2 * scale * sin((i - 90) * 3.14 / 180); dyi = dyo * 0.6;
    display.drawLine(dxo + x, dyo + y, dxi + x, dyi + y, color);
  }
}

void addrain(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  for (int i = 0; i < 6; i++) {
    display.fillCircle(x - scale * 4 + scale * i * 1.3, y + scale * 1.9, scale / 3, color);
    display.drawLine(x - scale * 4 + scale * i * 1.3, y + scale * 1.9,
                     x - scale * 3 + scale * i * 1.3, y + scale * 2.6, color);
  }
}

void addsnow(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  int dxo, dyo, dxi, dyi;
  for (int flakes = 0; flakes < 5; flakes++) {
    for (float i = 0; i < 360; i = i + 45) {
      dxo = scale * 0.5 * cos((i - 90) * 3.14 / 180); dxi = dxo * 0.1;
      dyo = scale * 0.5 * sin((i - 90) * 3.14 / 180); dyi = dyo * 0.1;
      display.drawLine(dxo + x + flakes * 1.5 * scale - scale * 3, dyo + y + scale * 2,
                       dxi + x + flakes * 1.5 * scale - scale * 3, dyi + y + scale * 2, color);
    }
  }
}

void addtstorm(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  // Draw lightning bolt
  display.drawLine(x - scale * 2, y + scale * 1.5, x - scale, y + scale * 1.9, color);
  display.drawLine(x - scale, y + scale * 1.9, x - scale * 1.5, y + scale * 2.2, color);
  display.drawLine(x - scale * 1.5, y + scale * 2.2, x - scale * 0.5, y + scale * 2.8, color);
}

void addmist(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  // Draw horizontal mist lines
  for (int i = 0; i < 4; i++) {
    display.fillRect(x - scale * 3, y + scale + i * scale * 0.5, scale * 6, 2, color);
  }
}

// Weather icon drawing functions with color support
void drawClearSky(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addsun(x, y, scale, color);
}

void drawFewClouds(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addcloud(x, y, scale, 2, color);
  addsun(x - scale * 1.8, y - scale * 1.8, scale * 0.7, color);
}

void drawClouds(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addcloud(x, y, scale, 2, color);
}

void drawRain(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addcloud(x, y, scale, 2, color);
  addrain(x, y, scale, color);
}

void drawSnow(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addcloud(x, y, scale, 2, color);
  addsnow(x, y, scale, color);
}

void drawThunderstorm(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addcloud(x, y, scale, 2, color);
  addtstorm(x, y, scale, color);
}

void drawMist(int x, int y, int scale, uint16_t color = GxEPD_BLACK) {
  addmist(x, y, scale, color);
}

// Function to draw weather icon based on OpenWeather icon code
void drawWeatherIcon(int x, int y, int scale, const char* iconCode, uint16_t color = GxEPD_BLACK) {
  if (!iconCode || strlen(iconCode) < 2) {
    drawClearSky(x, y, scale, color);
    return;
  }

  // Extract first 2 characters (ignore day/night indicator)
  char code[3] = {iconCode[0], iconCode[1], '\0'};

  if (strcmp(code, "01") == 0) drawClearSky(x, y, scale, color);        // Clear sky
  else if (strcmp(code, "02") == 0) drawFewClouds(x, y, scale, color);  // Few clouds
  else if (strcmp(code, "03") == 0) drawClouds(x, y, scale, color);     // Scattered clouds
  else if (strcmp(code, "04") == 0) drawClouds(x, y, scale, color);     // Broken clouds
  else if (strcmp(code, "09") == 0) drawRain(x, y, scale, color);       // Shower rain
  else if (strcmp(code, "10") == 0) drawRain(x, y, scale, color);       // Rain
  else if (strcmp(code, "11") == 0) drawThunderstorm(x, y, scale, color); // Thunderstorm
  else if (strcmp(code, "13") == 0) drawSnow(x, y, scale, color);       // Snow
  else if (strcmp(code, "50") == 0) drawMist(x, y, scale, color);       // Mist
  else drawClouds(x, y, scale, color);  // Default fallback
}

// Shadow callback function
void shadowCallback(const char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Parse shadow document
  JsonDocument shadowDoc;
  DeserializationError error = deserializeJson(shadowDoc, message);

  if (error) {
    Serial.print("[Shadow] Parse error: ");
    Serial.println(error.c_str());
    return;
  }

  // Extract desired state
  if (shadowDoc["state"]["desired"]["weather"]) {
    Serial.println("[Shadow] ✓ Weather data received");
    JsonDocument weatherDoc;
    weatherDoc.set(shadowDoc["state"]["desired"]["weather"]);
    serializeJson(weatherDoc, weatherData);
    weatherReceived = true;
  }

  if (shadowDoc["state"]["desired"]["calendar"]) {
    Serial.println("[Shadow] ✓ Calendar data received");
    JsonDocument calendarDoc;
    calendarDoc.set(shadowDoc["state"]["desired"]["calendar"]);
    serializeJson(calendarDoc, calendarData);
    calendarReceived = true;
  }
}

// MQTT callback function (fallback for direct topic messages)
void mqttCallback(const char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Check which topic and store accordingly
  String topicStr = String(topic);
  if (topicStr == CALENDAR_TOPIC) {
    Serial.println("[MQTT] ✓ Calendar data received");
    calendarData = message;
    calendarReceived = true;
  } else if (topicStr == WEATHER_TOPIC) {
    Serial.println("[MQTT] ✓ Weather data received");
    weatherData = message;
    weatherReceived = true;
  } else if (topicStr.indexOf("/shadow/") >= 0) {
    // Forward shadow messages to shadow callback
    shadowCallback(topic, payload, length);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n========================================");
  Serial.println("    E-Ink Family Calendar Starting");
  Serial.println("========================================\n");

  // Enable display power (HAT power control)
  Serial.println("[Display] Enabling HAT power control...");
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(100);  // Wait for power to stabilize

  // Initialize SPI with LOLIN32 hardware pins
  Serial.println("[SPI] Initializing SPI (SCK=18, MOSI=23)...");
  SPI.begin();  // Uses hardware SPI pins

  // Initialize display - let library handle reset
  Serial.println("[Display] Initializing display...");
  delay(100);

  display.init(115200);
  display.setRotation(3);  // Rotate 270 degrees for portrait orientation
  display.setTextColor(GxEPD_BLACK);

  // Connect to WiFi first (before loading screen)
  Serial.println("\n[WiFi] Connecting to network...");
  if (!wifi.connect()) {
    Serial.println("[WiFi] Connection failed!");
    // WiFi failed: Don't show loading screen, just clear WiFi bars and preserve calendar
    // E-ink is bistable - old calendar data remains visible
    showError("WiFi Connection Failed");
    goToSleep();
    return;
  }

  // WiFi connected: Show loading screen for full refresh (prevents e-ink ghosting)
  Serial.println("[WiFi] Connected! Showing loading screen...");
  showBootScreen();

  // Load AWS IoT certificates
  Serial.println("\n[AWS IoT] Loading certificates...");
  if (!awsIot.loadCertificates()) {
    Serial.println("[AWS IoT] Failed to load certificates!");
    showError("Certificate Load Failed");
    goToSleep();
    return;
  }

  // Connect to AWS IoT
  Serial.println("\n[AWS IoT] Connecting...");
  if (!awsIot.connect()) {
    Serial.println("[AWS IoT] Connection failed!");
    showError("AWS IoT Connection Failed");
    goToSleep();
    return;
  }

  // Set MQTT callback
  awsIot.setCallback(mqttCallback);

  // Subscribe to shadow updates (replaces direct topic subscriptions)
  Serial.println("[Shadow] Subscribing to shadow updates...");
  awsIot.subscribe("$aws/things/" + String(AWS_THING_NAME) + "/shadow/update/accepted");
  awsIot.subscribe("$aws/things/" + String(AWS_THING_NAME) + "/shadow/get/accepted");

  // Also subscribe to direct topics as fallback
  Serial.println("[MQTT] Subscribing to fallback topics...");
  awsIot.subscribe(CALENDAR_TOPIC);
  awsIot.subscribe(WEATHER_TOPIC);

  // Allow time for subscriptions to be acknowledged by broker
  Serial.println("[MQTT] Waiting for subscription confirmation...");
  for (int i = 0; i < 50; i++) {
    awsIot.loop();
    delay(100);  // Total 5 seconds to ensure subscriptions are ready
  }

  // Request current shadow state (gets persisted data)
  Serial.println("[Shadow] Requesting current shadow state...");
  awsIot.publish("$aws/things/" + String(AWS_THING_NAME) + "/shadow/get", "");

  // Publish ready status to trigger Lambdas (for immediate refresh)
  Serial.println("[MQTT] Publishing ready status...");
  String statusMsg = "{\"status\":\"ready\",\"timestamp\":\"" + String(awsIot.getTimestamp()) + "\"}";
  awsIot.publish("calendar/eink-calendar-01/status", statusMsg);

  // Give a moment for AWS to process and respond
  Serial.println("[MQTT] Waiting for AWS to process ready status...");
  for (int i = 0; i < 10; i++) {
    awsIot.loop();
    delay(100);  // Additional 1 second after publishing
  }

  // Wait for data from Shadow or Lambda (with longer timeout)
  Serial.println("[Data] Waiting for data from Shadow/Lambdas...");
  unsigned long startTime = millis();
  while ((!calendarReceived || !weatherReceived) && (millis() - startTime < 60000)) {
    awsIot.loop();
    delay(100);

    // Print progress every 5 seconds
    if ((millis() - startTime) % 5000 < 100) {
      Serial.print(".");
    }
  }
  Serial.println();

  // Report what was received
  Serial.print("[Data] Received - Calendar: ");
  Serial.print(calendarReceived ? "✓" : "✗");
  Serial.print(", Weather: ");
  Serial.println(weatherReceived ? "✓" : "✗");

  if (calendarReceived || weatherReceived) {
    displayCalendarAndWeather();
  } else {
    Serial.println("[Data] Timeout waiting for data");
    showError("Data Timeout");
  }

  // Disconnect and sleep
  awsIot.disconnect();
  goToSleep();
}

void loop() {
  // Not used - device sleeps after setup
}

void showBootScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw simple large circle in center
    int centerX = DISPLAY_WIDTH / 2;
    int centerY = DISPLAY_HEIGHT / 2;
    int radius = 100;

    // Draw thick circle outline
    for (int i = 0; i < 8; i++) {
      display.drawCircle(centerX, centerY, radius - i, GxEPD_BLACK);
    }

    // Draw loading text
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(centerX - 81, centerY + 10);  // Moved 0.3cm left (~11px)
    display.print("LOADING");
  } while (display.nextPage());
  delay(100);
  // Don't hibernate yet - we'll update the display soon
}

void showError(const char* message) {
  // Show WiFi error by clearing bars area (0 bars = no connection)
  Serial.print("[Display] Showing error indicator: ");
  Serial.println(message);

  // WiFi bars area (must match drawFooter() exactly!)
  int barX = DISPLAY_WIDTH - 77;  // Left edge of WiFi bars
  int barY = DISPLAY_HEIGHT - 51;  // Bottom of WiFi bars
  int barWidth = 6;
  int barSpacing = 2;
  int totalWidth = 4 * (barWidth + barSpacing) - barSpacing;  // 4 bars = 30px total
  int maxBarHeight = 6 + (3 * 4);  // Tallest bar = 18px

  // Partial update window with small margin
  int margin = 2;
  display.setPartialWindow(barX - margin, barY - maxBarHeight - margin,
                          totalWidth + 2 * margin, maxBarHeight + 2 * margin);
  display.firstPage();
  do {
    // Clear the WiFi bars area to show 0 bars (no connection)
    display.fillRect(barX - margin, barY - maxBarHeight - margin,
                    totalWidth + 2 * margin, maxBarHeight + 2 * margin, GxEPD_WHITE);
  } while (display.nextPage());
  delay(100);
  display.hibernate();
}

void displayCalendarAndWeather() {
  Serial.println("[Display] Rendering calendar and weather...");

  // Parse calendar data
  JsonDocument calendarDoc;
  DeserializationError calendarError = deserializeJson(calendarDoc, calendarData);

  // Parse weather data
  JsonDocument weatherDoc;
  DeserializationError weatherError = deserializeJson(weatherDoc, weatherData);

  if (calendarError) {
    Serial.print("[Display] Calendar parse error: ");
    Serial.println(calendarError.c_str());
  }

  if (weatherError) {
    Serial.print("[Display] Weather parse error: ");
    Serial.println(weatherError.c_str());
  }

  // Start drawing
  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);  // Default text color

    // Draw header
    drawHeader();

    // Draw events (if calendar data received)
    if (!calendarError && calendarDoc["events"].is<JsonArray>()) {
      JsonArray events = calendarDoc["events"].as<JsonArray>();
      drawEvents(events);
    } else {
      display.setFont(&FreeSansBold12pt7b);
      display.setCursor(43, EVENT_START_Y);
      display.print("No events");
    }

    // Draw weather (if weather data received)
    if (!weatherError) {
      drawWeather(weatherDoc);
    }

    // Draw footer
    drawFooter();

  } while (display.nextPage());

  Serial.println("[Display] Display updated successfully");

  // Allow display to complete refresh before hibernating
  delay(100);
  display.hibernate();
}

void drawHeader() {
  // Header removed completely to save vertical space
}

void drawEvents(JsonArray events) {
  int count = 0;

  for (JsonObject event : events) {
    if (count >= MAX_EVENTS) break;

    String title = String(event["title"] | "Untitled");
    String time = String(event["time"] | "");
    String description = String(event["description"] | "");
    bool isMultiday = event["multiday"] | false;

    // Replace special characters
    title = replaceSpecialChars(title);
    time = replaceSpecialChars(time);
    description = replaceSpecialChars(description);

    // Translate dates in time field
    time = shortenDate(time);

    // Truncate with ellipsis to prevent wrapping
    // Conservative limits for proportional font to fit on one line
    if (title.length() > 32) {
      title = title.substring(0, 29) + "...";
    }
    if (description.length() > 40) {
      description = description.substring(0, 37) + "...";
    }

    // Calculate position for this event block
    int x = 43;  // Left margin (0.5cm from left edge)
    int y = EVENT_START_Y + (count * EVENT_HEIGHT);

    // Multi-day indicator: Draw small arrow/line on the left
    if (isMultiday) {
      // Draw a small red bar on the left edge to indicate multi-day
      display.fillRect(33, y - 8, 2, 55, GxEPD_RED);
    }

    // Line 1: Date/Time (small font, RED if multiday for emphasis, otherwise black)
    display.setTextColor(isMultiday ? GxEPD_RED : GxEPD_BLACK);
    display.setFont(&FreeSans9pt7b);
    display.setCursor(x, y);
    display.print(time);

    // Line 2: Title (larger font, bold, RED for emphasis)
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(x, y + 20);  // Compact spacing
    display.print(title);

    // Line 3: Description (small font, black, indented for hierarchy)
    if (description.length() > 0) {
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeSans9pt7b);
      display.setCursor(x + 10, y + 38);  // Compact spacing, smaller indent
      display.print(description);
    }

    count++;
  }

  if (count == 0) {
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(43, EVENT_START_Y + 40);
    display.print("No upcoming events");
  }
}

void drawWeather(JsonDocument& weatherDoc) {
  // Draw current date at top right (with RED accent for emphasis)
  String date = String(weatherDoc["date"] | "Loading...");
  date = shortenDate(date);  // Shorten day names
  display.setTextColor(GxEPD_RED);
  display.setFont(&FreeSansBold12pt7b);
  // Moved 1.5cm to left
  display.setCursor(DISPLAY_WIDTH - 282, 130);
  display.print(date);

  JsonArray forecast = weatherDoc["forecast"].as<JsonArray>();
  if (forecast.size() > 0 && forecast.size() >= 1) {
    // LEFT SIDE: Current weather - Large and prominent
    JsonObject current = forecast[0];
    int currentTemp = current["temp"] | 0;
    const char* currentIcon = current["icon"] | "01d";

    // Current weather icon - HUGE (scale 14) - 2x bigger
    drawWeatherIcon(91, 138, 14, currentIcon, GxEPD_RED);

    // Current temperature - Large RED
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(48, 196);
    display.print(currentTemp);
    display.setFont(&FreeSansBold12pt7b);
    display.print("C");

    // RIGHT SIDE: Future forecast (3 slots: +6h, +12h, +18h)
    // Position them below the date, in a horizontal row
    int forecastY = 158;  // Position below date (moved down 2.5cm)
    int startX = DISPLAY_WIDTH - 294;  // Start from right area (moved 2cm right)
    int spacing = 95;  // Spacing between forecast items (compact)

    for (int i = 1; i < min((int)forecast.size(), 4); i++) {
      JsonObject fc = forecast[i];
      int fcTemp = fc["temp"] | 0;
      const char* fcIcon = fc["icon"] | "01d";
      int fcHour = fc["hour"] | 0;

      int x = startX + ((i - 1) * spacing);  // i-1 because we skip the first (current)

      // Time label (black for future forecasts)
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeSans9pt7b);
      display.setCursor(x, forecastY);
      if (fcHour < 10) display.print("0");
      display.print(fcHour);
      display.print(":00");

      // Weather icon (medium size, scale 3, black) - moved down 5px
      drawWeatherIcon(x + 8, forecastY + 13, 3, fcIcon, GxEPD_BLACK);

      // Temperature (black, aligned) - moved down 5px
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeSans9pt7b);
      display.setCursor(x + 5, forecastY + 48);
      display.print(fcTemp);
      display.print("C");
    }
  }

  // Draw separator line between header and events
  display.drawLine(28, HEADER_HEIGHT + 8, DISPLAY_WIDTH - 10, HEADER_HEIGHT + 8, GxEPD_BLACK);
}

void drawFooter() {
  // Show WiFi signal bars at bottom-right
  int rssi = wifi.getRSSI();
  int bars = 0;
  if (rssi >= -50) bars = 4;
  else if (rssi >= -60) bars = 3;
  else if (rssi >= -70) bars = 2;
  else if (rssi >= -80) bars = 1;

  // Draw bars as filled rectangles at bottom-right
  int barX = DISPLAY_WIDTH - 77;  // Right side, moved 0.5cm left
  int barY = DISPLAY_HEIGHT - 51;  // Bottom, moved 6cm down
  int barWidth = 6;  // Width of each bar
  int barSpacing = 2;  // Space between bars

  for (int i = 0; i < 4; i++) {
    int barHeight = 6 + (i * 4);  // Increasing heights: 6, 10, 14, 18
    if (i < bars) {
      // Draw filled bar
      display.fillRect(barX + (i * (barWidth + barSpacing)), barY - barHeight, barWidth, barHeight, GxEPD_BLACK);
    }
  }
}

void goToSleep() {
  Serial.println("\n[Sleep] Going to deep sleep for ");
  Serial.print(SLEEP_TIME_MINUTES);
  Serial.println(" minutes");

  wifi.disconnect();

  // Deep sleep (time in microseconds)
  uint64_t sleepTime = SLEEP_TIME_MINUTES * 60 * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTime);

  Serial.println("[Sleep] Goodnight!\n");
  Serial.flush();

  esp_deep_sleep_start();
}
