#include <Arduino.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>

// Configuration - consider moving to a separate config.h file
#define DEBUG true  // Set to false to disable Serial debugging in production

// Pin definitions
constexpr uint8_t PN532_IRQ = 15;
constexpr uint8_t PN532_RESET = 16;
constexpr uint8_t BUTTON_PIN = 0;

// API Configuration - consider using HTTPS for production
const char* API_BASE_URL = "http://192.168.43.202:8000/api";
const char* TERMINAL_ID = "1";
const char* API_TOKEN = "1234567890abcdef"; // Consider secure storage solutions for production

// Optimized time constants (in milliseconds)
constexpr unsigned long CARD_SCAN_DELAY = 500;            // Minimum delay between card scans
constexpr unsigned long DEBOUNCE_DELAY = 200;             // Button debounce delay
constexpr unsigned long DISPLAY_TIME = 1500;              // Message display time
constexpr unsigned long RETRY_DELAY = 3000;               // Connection retry delay
constexpr unsigned long TERMINAL_CHECK_INTERVAL = 600000; // Check terminal auth every 10 minutes
constexpr unsigned long WIFI_CHECK_INTERVAL = 10000;      // WiFi check interval
constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 5000;   // Display refresh interval
constexpr unsigned long HTTP_TIMEOUT = 8000;              // HTTP request timeout (8 seconds)

// Operation modes
enum class OperationMode {
  PAYMENT = 0,
  ACCESS = 1
};

// Global objects
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiClient;
ESP8266WebServer server(80); // Added for local API
Ticker wifiChecker;          // For periodic WiFi checks

// Status variables
struct SystemState {
  OperationMode mode = OperationMode::PAYMENT;
  float paymentAmount = 5000.0;
  String cardUID;
  String lastCardUID;
  unsigned long lastCardReadTime = 0;
  bool isProcessing = false;
  bool isWiFiConnected = false;
  unsigned long lastTerminalCheck = 0;
  unsigned long lastDisplayUpdate = 0;
  
  // New fields for improved operation
  bool initComplete = false;
  bool cardReadError = false;
};

// Terminal capabilities (populated from server during authentication)
struct TerminalCapabilities {
  bool supports_payment = false;
  bool supports_topup = false;
  bool supports_transfer = false;
  bool supports_access = false;
  bool is_active = false;
  String allowed_user_types[3];  // student, lecture, staff
  int allowed_types_count = 0;
  
  // Add a timeout for next authentication attempt
  unsigned long nextAuthAttempt = 0;
};

// Global state instances
SystemState state;
TerminalCapabilities terminal;

// Function forward declarations
void setupWiFi();
void setupNFC();
void setupLCD();
void setupWebServer();
bool authenticateTerminal();
bool readCard();
void processCard();
void updateDisplay();
void handleButton();
void showMessage(const String &line1, const String &line2 = "", int delayMs = 0);
bool isWiFiConnected();
void reconnectWiFi();
void checkWiFiStatus();
int sendRequest(const String &endpoint, const String &method, const String &data, String *response = nullptr);
void logDebug(const String &message);

// Transaction functions
String getCardInfo(const String &uid, bool &isBlocked, String &userType, bool &isUserActive);
bool processPayment(const String &uid, float amount);
bool checkAccess(const String &uid);
void recordAccess(const String &uid);
bool validateCard(const String &uid, String &userType, bool &isUserActive);

// Web server handlers
void handleRoot();
void handleStatus();
void handleNotFound();

// Error handling
String getLastErrorMessage();
void resetSystem();

/**
 * Main setup function
 */
void setup() {
  #if DEBUG
    Serial.begin(115200);
    Serial.println(F("\n=== Smart Card Terminal System ==="));
  #endif

  // Initialize hardware components
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setupLCD();
  setupWiFi();
  setupNFC();
  setupWebServer();

  // Authenticate terminal with server
  if (authenticateTerminal()) {
    showMessage(F("Terminal Aktif"), F("Tempel Kartu"));
    state.initComplete = true;
  } else {
    showMessage(F("Auth Gagal"), F("Restart sistem"));
    delay(5000); // Give time to read message
    resetSystem();
  }
  
  // Initialize periodic WiFi checks
  wifiChecker.attach_ms(WIFI_CHECK_INTERVAL, checkWiFiStatus);
}

/**
 * Main loop function
 */
void loop() {
  // Handle HTTP requests
  server.handleClient();
  
  // If initialization failed, don't proceed with main functionality
  if (!state.initComplete) {
    delay(100);
    ESP.wdtFeed();
    return;
  }

  // Periodic terminal authentication check
  if (millis() - state.lastTerminalCheck > TERMINAL_CHECK_INTERVAL) {
    state.lastTerminalCheck = millis();
    authenticateTerminal();
  }

  // Check button state
  handleButton();

  // Keep display updated
  if (millis() - state.lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    state.lastDisplayUpdate = millis();
    updateDisplay();
  }

  // Read Card with consistent interval
  if (!state.isProcessing && (millis() - state.lastCardReadTime > CARD_SCAN_DELAY)) {
    if (readCard()) {
      // Validate Card before processing
      String userType;
      bool isUserActive;
      if (validateCard(state.cardUID, userType, isUserActive)) {
        processCard();
      } else {
        if (!isUserActive) {
          showMessage(F("User Nonaktif"), F("Akses ditolak"));
        } else {
          showMessage(F("Kartu Diblokir"), F("Hubungi admin"));
        }
        delay(DISPLAY_TIME);
        updateDisplay();
      }
    }
  }

  // Feed watchdog
  ESP.wdtFeed();
  yield();
}

/**
 * Initialize LCD display
 */
void setupLCD() {
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Smart Card System"));
  lcd.setCursor(0, 1);
  lcd.print(F("Memulai..."));
  delay(1000);
}

/**
 * Initialize WiFi connection
 */
void setupWiFi() {
  showMessage(F("WiFi Setup"), F("Menghubungkan..."));

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);  // 3 minute timeout

  // Add custom parameters here if needed
  // WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  // wifiManager.addParameter(&custom_mqtt_server);

  // Use a unique AP name including chip ID for multiple devices
  String apName = "SmartCard_" + String(ESP.getChipId(), HEX);
  
  if (!wifiManager.autoConnect(apName.c_str(), "password123")) {
    showMessage(F("WiFi Gagal"), F("Restart..."));
    delay(3000);
    ESP.restart();
  }

  state.isWiFiConnected = true;
  showMessage(F("WiFi Terhubung"), WiFi.localIP().toString());
  delay(1000);
}

/**
 * Initialize NFC reader
 */
void setupNFC() {
  showMessage(F("NFC Reader"), F("Inisialisasi..."));

  nfc.begin();
  SPI.begin();

  for (int attempt = 0; attempt < 3; attempt++) {
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
      char versionStr[10];
      sprintf(versionStr, "%d.%d", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
      showMessage(F("RFID Reader OK"), F("Versi: ") + String(versionStr));

      nfc.SAMConfig();
      delay(1000);
      return;
    }
    delay(500);
  }

  showMessage(F("NFC Error"), F("Reader tidak terdeteksi"));
  delay(5000); // Give time to read message
  resetSystem();
}

/**
 * Setup web server for local API access
 */
void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  logDebug("Web server started on port 80");
}

/**
 * Handle root page request
 */
void handleRoot() {
  String html = "<html><head><title>Smart Card Terminal</title></head><body>";
  html += "<h1>Smart Card Terminal</h1>";
  html += "<p>Status: " + String(state.isWiFiConnected ? "Online" : "Offline") + "</p>";
  html += "<p>Mode: " + String(state.mode == OperationMode::PAYMENT ? "Payment" : "Access") + "</p>";
  html += "<p><a href='/status'>View Status</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

/**
 * Handle status page request
 */
void handleStatus() {
  StaticJsonDocument<512> doc;
  doc["wifi_connected"] = state.isWiFiConnected;
  doc["mode"] = state.mode == OperationMode::PAYMENT ? "payment" : "access";
  doc["payment_amount"] = state.paymentAmount;
  doc["terminal_active"] = terminal.is_active;
  doc["supports_payment"] = terminal.supports_payment;
  doc["supports_access"] = terminal.supports_access;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

/**
 * Handle 404 not found requests
 */
void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

/**
 * Authenticate terminal with server
 * @return True if authentication successful, false otherwise
 */
bool authenticateTerminal() {
  // Don't retry auth too frequently if failed
  if (millis() < terminal.nextAuthAttempt) {
    return terminal.is_active;
  }

  if (!isWiFiConnected()) {
    terminal.nextAuthAttempt = millis() + RETRY_DELAY;
    return false;
  }

  showMessage(F("Autentikasi"), F("Terminal..."));

  StaticJsonDocument<256> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["device_id"] = String(ESP.getChipId());

  String payload;
  serializeJson(doc, payload);

  String response;
  int httpCode = sendRequest("/terminal/authenticate", "POST", payload, &response);

  if (httpCode == 200) {
    StaticJsonDocument<512> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    if (!error && respDoc["success"].as<bool>()) {
      // Set terminal capabilities from server response
      JsonObject data = respDoc["data"]["terminal"];
      terminal.supports_payment = data["supports_payment"];
      terminal.supports_topup = data["supports_topup"];
      terminal.supports_transfer = data["supports_transfer"];
      terminal.supports_access = data["supports_access"];
      terminal.is_active = data["is_active"];

      // Get allowed user types
      terminal.allowed_types_count = 0;
      if (data.containsKey("allowed_user_types")) {
        JsonArray allowed_types = data["allowed_user_types"];
        for (int i = 0; i < allowed_types.size() && i < 3; i++) {
          terminal.allowed_user_types[i] = allowed_types[i].as<String>();
          terminal.allowed_types_count++;
        }
      }

      // Set lastTerminalCheck timestamp
      state.lastTerminalCheck = millis();

      // Log terminal capabilities
      logDebug("Terminal capabilities:");
      logDebug(" - Payment: " + String(terminal.supports_payment ? "Yes" : "No"));
      logDebug(" - Topup: " + String(terminal.supports_topup ? "Yes" : "No"));
      logDebug(" - Transfer: " + String(terminal.supports_transfer ? "Yes" : "No"));
      logDebug(" - Access: " + String(terminal.supports_access ? "Yes" : "No"));

      logDebug("Allowed user types:");
      for (int i = 0; i < terminal.allowed_types_count; i++) {
        logDebug(" - " + terminal.allowed_user_types[i]);
      }

      return terminal.is_active;
    }
  }

  // Set retry timeout
  terminal.nextAuthAttempt = millis() + RETRY_DELAY;
  return false;
}

/**
 * Read card from NFC reader
 * @return True if card was read successfully, false otherwise
 */
bool readCard() {
  if (millis() - state.lastCardReadTime < CARD_SCAN_DELAY) {
    return false;
  }

  uint8_t uidBuffer[7];
  uint8_t uidLength;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength, 100)) {
    // No card detected, but not an error
    return false;
  }

  if (uidLength == 0 || uidLength > 7) {
    state.cardReadError = true;
    logDebug("Invalid UID length: " + String(uidLength));
    return false;
  }

  // Format UID
  String uid;
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uidBuffer[i] < 0x10) {
      uid += "0";
    }
    uid += String(uidBuffer[i], HEX);
    if (i < uidLength - 1) {
      uid += ":";
    }
  }
  uid.toUpperCase();

  // Avoid duplicate reads in quick succession
  if (uid == state.lastCardUID && (millis() - state.lastCardReadTime < 3000)) {
    return false;
  }

  state.cardUID = uid;
  state.lastCardUID = uid;
  state.lastCardReadTime = millis();
  state.cardReadError = false;

  logDebug("Card detected: " + uid);
  showMessage(F("Kartu Terdeteksi"), uid.substring(0, 10) + "...");
  delay(800);

  return true;
}

/**
 * Validate card with server
 * @param uid Card UID
 * @param userType Output parameter for user type
 * @param isUserActive Output parameter for user active status
 * @return True if card is valid, false otherwise
 */
bool validateCard(const String &uid, String &userType, bool &isUserActive) {
  bool isBlocked = false;
  String cardInfo = getCardInfo(uid, isBlocked, userType, isUserActive);

  // Card is valid if not blocked and user is active
  if (!isBlocked && isUserActive) {
    bool userTypeAllowed = false;

    // Check if user type is allowed for this terminal
    for (int i = 0; i < terminal.allowed_types_count; i++) {
      if (terminal.allowed_user_types[i] == userType) {
        userTypeAllowed = true;
        break;
      }
    }

    if (!userTypeAllowed) {
      showMessage(F("Hak Akses"), F("Tidak Diizinkan"));
      delay(DISPLAY_TIME);
      return false;
    }

    return true;
  }

  return false;
}

/**
 * Process card based on current mode
 */
void processCard() {
  state.isProcessing = true;
  String userType;
  bool isUserActive;
  bool isBlocked;
  String cardInfo = getCardInfo(state.cardUID, isBlocked, userType, isUserActive);

  // Payment Mode
  if (state.mode == OperationMode::PAYMENT) {
    // Check if terminal supports payment
    if (!terminal.supports_payment) {
      showMessage(F("Terminal Ini"), F("Tidak Support Payment"));
      delay(DISPLAY_TIME);
      state.isProcessing = false;
      updateDisplay();
      return;
    }

    showMessage(F("Memproses"), F("Pembayaran..."));
    if (processPayment(state.cardUID, state.paymentAmount)) {
      showMessage(F("Pembayaran Sukses"), F("Jumlah: ") + String(state.paymentAmount));

      // Display balance after successful payment
      delay(DISPLAY_TIME);
      String userType;
      bool isUserActive;
      bool isBlocked;
      String updatedCardInfo = getCardInfo(state.cardUID, isBlocked, userType, isUserActive);
      showMessage(F("Info Kartu"), updatedCardInfo);
    } else {
      showMessage(F("Pembayaran Gagal"), F("Coba lagi"));
    }
    delay(DISPLAY_TIME);
  }
  // Access Mode
  else if (state.mode == OperationMode::ACCESS) {
    // Check if terminal supports access
    if (!terminal.supports_access) {
      showMessage(F("Terminal Ini"), F("Tidak Support Akses"));
      delay(DISPLAY_TIME);
      state.isProcessing = false;
      updateDisplay();
      return;
    }

    showMessage(F("Memverifikasi"), F("Akses..."));
    if (checkAccess(state.cardUID)) {
      showMessage(F("Akses Diizinkan"), F("Silakan masuk"));
      recordAccess(state.cardUID);

      // Display user info
      delay(DISPLAY_TIME);
      showMessage(F("User: "), cardInfo);
    } else {
      showMessage(F("Akses Ditolak"), F("Hubungi admin"));
    }
    delay(DISPLAY_TIME);
  }

  state.isProcessing = false;
  updateDisplay();
}

/**
 * Update display based on current mode
 */
void updateDisplay() {
  switch (state.mode) {
    case OperationMode::PAYMENT:
      if (terminal.supports_payment) {
        showMessage(F("Mode Pembayaran"), F("Jumlah: ") + String(state.paymentAmount));
      } else {
        showMessage(F("Mode Pembayaran"), F("Tidak Support"));
      }
      break;
    case OperationMode::ACCESS:
      if (terminal.supports_access) {
        showMessage(F("Mode Akses"), F("Tempel Kartu"));
      } else {
        showMessage(F("Mode Akses"), F("Tidak Support"));
      }
      break;
  }
}

/**
 * Handle button press for mode toggle
 */
void handleButton() {
  static bool lastButtonState = false;
  static unsigned long lastDebounceTime = 0;
  static bool debouncedButtonState = false;
  static bool lastDebouncedState = false;

  bool currentButtonState = digitalRead(BUTTON_PIN) == LOW;

  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();
  }
  lastButtonState = currentButtonState;

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    debouncedButtonState = currentButtonState;

    if (debouncedButtonState && !lastDebouncedState) {
      logDebug("Button pressed - switching mode");

      // Toggle between payment and access modes
      if (state.mode == OperationMode::PAYMENT) {
        state.mode = OperationMode::ACCESS;
      } else {
        state.mode = OperationMode::PAYMENT;
      }

      updateDisplay();
    }

    lastDebouncedState = debouncedButtonState;
  }
}

/**
 * Get card information from server
 * @param uid Card UID
 * @param isBlocked Output parameter for card blocked status
 * @param userType Output parameter for user type
 * @param isUserActive Output parameter for user active status
 * @return Card information string
 */
String getCardInfo(const String &uid, bool &isBlocked, String &userType, bool &isUserActive) {
  if (!isWiFiConnected()) {
    isBlocked = true;
    isUserActive = false;
    userType = "";
    return "No WiFi";
  }

  String response;
  String endpoint = "/terminal/cards/info?card_uid=" + uid + "&terminal_id=" + String(TERMINAL_ID);
  int httpCode = sendRequest(endpoint, "GET", "", &response);

  if (httpCode == 200) {
    StaticJsonDocument<512> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    if (!error && respDoc["success"].as<bool>()) {
      String userName = respDoc["data"]["user"]["name"].as<String>();
      userType = respDoc["data"]["user"]["role_type"].as<String>();
      float balance = respDoc["data"]["card"]["balance"];
      isBlocked = respDoc["data"]["card"]["is_blocked"];

      // Assuming user status is included in the response
      isUserActive = (respDoc["data"]["user"].containsKey("status") && 
                     respDoc["data"]["user"]["status"] == "active");

      if (isBlocked) {
        return userName + " [BLOKIR]";
      } else {
        return userName + " Rp." + String(balance, 0);
      }
    }
  }

  isBlocked = true;  // Default to blocked if can't get info
  isUserActive = false;
  userType = "";
  return "";
}

/**
 * Process payment transaction
 * @param uid Card UID
 * @param amount Payment amount
 * @return True if payment successful, false otherwise
 */
bool processPayment(const String &uid, float amount) {
  if (!isWiFiConnected()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;
  doc["amount"] = amount;
  doc["notes"] = F("Pembayaran di terminal ") + String(TERMINAL_ID);
  doc["transaction_time"] = millis(); // Add timestamp for better tracking

  String payload;
  serializeJson(doc, payload);

  String response;
  int httpCode = sendRequest("/terminal/process-payment", "POST", payload, &response);

  if (httpCode == 200) {
    StaticJsonDocument<256> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    return (!error && respDoc["success"].as<bool>());
  }

  return false;
}

/**
 * Check access permission
 * @param uid Card UID
 * @return True if access granted, false otherwise
 */
bool checkAccess(const String &uid) {
  if (!isWiFiConnected()) {
    return false;
  }

  StaticJsonDocument<128> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;
  doc["device_id"] = String(ESP.getChipId());

  String payload;
  serializeJson(doc, payload);

  String response;
  int httpCode = sendRequest("/terminal/access/verify", "POST", payload, &response);

  if (httpCode == 200) {
    StaticJsonDocument<256> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    return (!error && respDoc["success"].as<bool>());
  }

  return false;
}

/**
 * Record access event in the server
 * @param uid Card UID
 */
void recordAccess(const String &uid) {
  if (!isWiFiConnected()) {
    return;
  }

  StaticJsonDocument<256> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;
  doc["notes"] = F("Akses di terminal ") + String(TERMINAL_ID);
  doc["timestamp"] = millis();
  doc["device_id"] = String(ESP.getChipId());

  String payload;
  serializeJson(doc, payload);

  sendRequest("/terminal/transactions/record-access", "POST", payload);
}

/**
 * Send HTTP request to server
 * @param endpoint API endpoint
 * @param method HTTP method (GET or POST)
 * @param data Request data
 * @param response Output parameter for response
 * @return HTTP response code
 */
int sendRequest(const String &endpoint, const String &method, const String &data, String *response) {
  if (!isWiFiConnected()) {
    return -1;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + endpoint;

  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(API_TOKEN));
  http.addHeader("X-Device-ID", String(ESP.getChipId()));
  http.setTimeout(HTTP_TIMEOUT);

  int httpCode = -1;

  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(data);
  }

  if (httpCode > 0 && response != nullptr) {
    *response = http.getString();
  } else if (httpCode <= 0) {
    logDebug("HTTP request failed with code: " + String(httpCode));
  }

  http.end();
  return httpCode;
}


/**
 * Display message on LCD
 * @param line1 First line text
 * @param line2 Second line text
 * @param delayMs Optional delay after showing message
 */
void showMessage(const String &line1, const String &line2, int delayMs) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);

  if (line2.length() > 0) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }

  if (delayMs > 0) {
    delay(delayMs);
  }
}

/**
 * Check if WiFi is connected
 * @return True if connected, false otherwise
 */
bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED && state.isWiFiConnected;
}

/**
 * Periodic WiFi status check function
 * Called by Ticker timer
 */
void checkWiFiStatus() {
  if (!isWiFiConnected()) {
    reconnectWiFi();
  }
}

/**
 * Attempt to reconnect to WiFi
 */
void reconnectWiFi() {
  // Don't try reconnection during processing
  if (state.isProcessing) {
    return;
  }
  
  showMessage(F("WiFi Terputus"), F("Menyambung ulang..."));

  WiFi.reconnect();

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 6000) {
    delay(500);
    ESP.wdtFeed();
  }

  if (WiFi.status() == WL_CONNECTED) {
    state.isWiFiConnected = true;
    showMessage(F("WiFi Terhubung"), WiFi.localIP().toString());
    delay(1000);
    updateDisplay();
  } else {
    state.isWiFiConnected = false;
    showMessage(F("WiFi Gagal"), F("Coba lagi nanti"));
    delay(2000);
    updateDisplay();
  }
}

/**
 * Log debug message to Serial
 * @param message Message to log
 */
void logDebug(const String &message) {
  #if DEBUG
    Serial.println(message);
  #endif
}

/**
 * Reset the system
 */
void resetSystem() {
  showMessage(F("System Reset"), F("Restarting..."));
  delay(2000);
  ESP.restart();
}

/**
 * Get last error message
 * @return Error message
 */
String getLastErrorMessage() {
  if (state.cardReadError) {
    return F("Card Read Error");
  }
  if (!state.isWiFiConnected) {
    return F("WiFi Disconnected");
  }
  if (!terminal.is_active) {
    return F("Terminal Inactive");
  }
  return F("Unknown Error");
}