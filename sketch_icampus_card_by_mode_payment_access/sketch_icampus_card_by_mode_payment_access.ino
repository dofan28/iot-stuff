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
#include <FS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// Definisi pin
#define PN532_IRQ 15
#define PN532_RESET 16
#define BUTTON_PIN 0
#define STATUS_LED_PIN 2  // LED bawaan NodeMCU

// Konfigurasi API
struct ApiConfig {
  String baseUrl = "http://icampuscard.taibacreative.co.id/api";
  String terminalId = "1";
  String apiToken = "1234567890abcdef";
};

// Konstanta waktu yang dioptimalkan
const unsigned long CARD_SCAN_DELAY = 500;             // Jeda minimum antar pemindaian kartu (ms)
const unsigned long DEBOUNCE_DELAY = 200;              // Jeda debounce tombol (ms)
const unsigned long DISPLAY_TIME = 1500;               // Waktu tampilan pesan (ms)
const unsigned long RETRY_DELAY = 3000;                // Jeda percobaan koneksi ulang (ms)
const unsigned long TERMINAL_CHECK_INTERVAL = 600000;  // Periksa autentikasi terminal setiap 10 menit
const unsigned long WIFI_CHECK_INTERVAL = 30000;       // Periksa koneksi WiFi setiap 30 detik
const unsigned long NTP_UPDATE_INTERVAL = 3600000;     // Update waktu dari NTP setiap jam

// Mode operasi
enum OperationMode {
  MODE_PAYMENT = 0,
  MODE_ACCESS = 1,
  MODE_SETTINGS = 2
};

// Status transaksi
enum TransactionStatus {
  IDLE,
  PROCESSING,
  SUCCESS,
  FAILED
};

// Global objects
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiClient;
ESP8266WebServer server(80);
ApiConfig apiConfig;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Variabel status
struct {
  OperationMode mode = MODE_PAYMENT;
  float paymentAmount = 5000.0;
  String cardUID;
  String lastCardUID;
  unsigned long lastCardReadTime = 0;
  TransactionStatus transactionStatus = IDLE;
  bool isWiFiConnected = false;
  unsigned long lastTerminalCheck = 0;
  unsigned long lastWiFiCheck = 0;
  unsigned long lastNtpUpdate = 0;
  unsigned long transactionStartTime = 0;
  String currentDisplayMsg[2] = { "", "" };
} state;

// Kapabilitas terminal (diisi dari server selama autentikasi)
struct {
  bool supports_payment = false;
  bool supports_topup = false;
  bool supports_transfer = false;
  bool supports_access = false;
  bool is_active = false;
  String allowed_user_types[3];  // student, lecture, staff
  int allowed_types_count = 0;
} terminal;

// Deklarasi fungsi
void setupWiFi();
void setupNFC();
void setupLCD();
void setupWebServer();
void setupSPIFFS();
void setupNTP();
bool loadConfig();
void saveConfig();
bool authenticateTerminal();
bool readCard();
void processCard();
void updateDisplay();
void handleButton();
void showMessage(const String &line1, const String &line2 = "", int delayMs = 0, bool log = true);
bool isWiFiConnected();
void reconnectWiFi();
int sendRequest(const String &endpoint, const String &method, const String &data, String *response = nullptr);
void updateNtpTime();
unsigned long getTimestamp();

// Fungsi transaksi
String getCardInfo(const String &uid, bool &isBlocked, String &userType, bool &isUserActive);
bool processPayment(const String &uid, float amount);
bool checkAccess(const String &uid);
void recordAccess(const String &uid);
bool validateCard(const String &uid, String &userType, bool &isUserActive);

// Handler web server
void handleRoot();
void handleSettings();
void handleSaveSettings();
void handleRestart();
void handleSystemInfo();

void setup() {
  // Inisialisasi komunikasi serial
  Serial.begin(115200);
  Serial.println(F("\n\n=== Smart Card Terminal System ==="));
  Serial.println(F("Versi: 2.0.0"));
  Serial.println(F("Memulai inisialisasi sistem..."));

  // Setup pin dan LED
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH);  // LED mati (logika terbalik pada NodeMCU)

  // Inisialisasi sistem file
  setupSPIFFS();

  // Muat konfigurasi
  if (loadConfig()) {
    Serial.println(F("Konfigurasi berhasil dimuat dari penyimpanan"));
  } else {
    Serial.println(F("Menggunakan konfigurasi default"));
  }

  // Setup LCD
  setupLCD();

  // Setup WiFi
  setupWiFi();

  // Setup NTP Client
  setupNTP();

  // Setup NFC
  setupNFC();

  // Setup web server untuk konfigurasi
  setupWebServer();

  // Autentikasi terminal
  if (authenticateTerminal()) {
    showMessage(F("Terminal Aktif"), F("Tempel Kartu"));
  } else {
    showMessage(F("Auth Gagal"), F("Restart/Cek WiFi"));
    digitalWrite(STATUS_LED_PIN, LOW);  // LED nyala menandakan error
  }
}

void loop() {
  // Layani permintaan web
  server.handleClient();

  // Periksa koneksi WiFi secara berkala
  if (millis() - state.lastWiFiCheck > WIFI_CHECK_INTERVAL) {
    state.lastWiFiCheck = millis();
    if (!isWiFiConnected()) {
      reconnectWiFi();
    }
  }

  // Update waktu NTP secara berkala
  if (millis() - state.lastNtpUpdate > NTP_UPDATE_INTERVAL) {
    state.lastNtpUpdate = millis();
    updateNtpTime();
  }

  // Periksa autentikasi terminal secara berkala
  if (millis() - state.lastTerminalCheck > TERMINAL_CHECK_INTERVAL && isWiFiConnected()) {
    state.lastTerminalCheck = millis();
    authenticateTerminal();
  }

  // Periksa tombol
  handleButton();

  // Perbarui tampilan jika diperlukan
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }

  // Timeout pada proses transaksi
  if (state.transactionStatus == PROCESSING && (millis() - state.transactionStartTime > 10000)) {
    state.transactionStatus = FAILED;
    showMessage(F("Timeout"), F("Transaksi gagal"));
    delay(DISPLAY_TIME);
    updateDisplay();
  }

  // Baca kartu dengan interval konsisten
  if (state.transactionStatus == IDLE && (millis() - state.lastCardReadTime > CARD_SCAN_DELAY)) {
    if (readCard()) {
      // Validasi kartu sebelum memproses
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

  // Status LED: berkedip lambat saat terhubung ke WiFi, cepat saat memproses
  static unsigned long lastBlinkTime = 0;
  static bool ledState = false;

  if (state.transactionStatus == PROCESSING) {
    // Berkedip cepat saat memproses
    if (millis() - lastBlinkTime > 100) {
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState);
    }
  } else if (isWiFiConnected()) {
    // Berkedip lambat saat terhubung ke WiFi
    if (millis() - lastBlinkTime > 1000) {
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState);
    }
  } else {
    // Nyala terus saat tidak terhubung WiFi
    digitalWrite(STATUS_LED_PIN, LOW);
  }

  // Feed watchdog
  ESP.wdtFeed();
  yield();
}

void setupSPIFFS() {
  if (SPIFFS.begin()) {
    Serial.println(F("SPIFFS berhasil dimuat"));
  } else {
    Serial.println(F("Gagal memuat SPIFFS"));
    SPIFFS.format();
    if (SPIFFS.begin()) {
      Serial.println(F("SPIFFS berhasil diformat dan dimuat"));
    } else {
      Serial.println(F("Gagal memformat SPIFFS"));
    }
  }
}

bool loadConfig() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, buf.get());

      if (!error) {
        apiConfig.baseUrl = doc["api_base_url"].as<String>();
        apiConfig.terminalId = doc["terminal_id"].as<String>();
        apiConfig.apiToken = doc["api_token"].as<String>();
        state.paymentAmount = doc["default_payment"].as<float>();

        Serial.println(F("Konfigurasi dimuat:"));
        Serial.println(" - API Base URL: " + apiConfig.baseUrl);
        Serial.println(" - Terminal ID: " + apiConfig.terminalId);
        Serial.println(" - Default Payment: " + String(state.paymentAmount));

        configFile.close();
        return true;
      } else {
        Serial.println(F("Gagal memuat konfigurasi: ") + String(error.c_str()));
      }
      configFile.close();
    }
  }
  return false;
}

void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["api_base_url"] = apiConfig.baseUrl;
  doc["terminal_id"] = apiConfig.terminalId;
  doc["api_token"] = apiConfig.apiToken;
  doc["default_payment"] = state.paymentAmount;

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
    Serial.println(F("Konfigurasi disimpan"));
  } else {
    Serial.println(F("Gagal membuka config.json untuk menulis"));
  }
}

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

void setupWiFi() {
  showMessage(F("WiFi Setup"), F("Menghubungkan..."));

  // Konfigurasikan WiFiManager
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);  // 3 menit timeout

  // Tambahkan parameter kustom
  WiFiManagerParameter custom_api_url("api_url", "API Base URL", apiConfig.baseUrl.c_str(), 100);
  WiFiManagerParameter custom_terminal_id("terminal_id", "Terminal ID", apiConfig.terminalId.c_str(), 10);
  WiFiManagerParameter custom_api_token("api_token", "API Token", apiConfig.apiToken.c_str(), 50);

  wifiManager.addParameter(&custom_api_url);
  wifiManager.addParameter(&custom_terminal_id);
  wifiManager.addParameter(&custom_api_token);

  // Coba hubungkan atau buat portal konfigurasi
  if (!wifiManager.autoConnect("SmartCard_AP", "password123")) {
    showMessage(F("WiFi Gagal"), F("Restart..."));
    delay(3000);
    ESP.restart();
  }

  // Dapatkan parameter kustom
  apiConfig.baseUrl = custom_api_url.getValue();
  apiConfig.terminalId = custom_terminal_id.getValue();
  apiConfig.apiToken = custom_api_token.getValue();

  // Simpan konfigurasi
  saveConfig();

  state.isWiFiConnected = true;
  showMessage(F("WiFi Terhubung"), WiFi.localIP().toString());
  delay(1000);
}

void setupNTP() {
  timeClient.begin();
  timeClient.setTimeOffset(7 * 3600);  // GMT+7 (WIB)
  updateNtpTime();
}

void updateNtpTime() {
  if (!isWiFiConnected()) return;

  if (timeClient.update()) {
    state.lastNtpUpdate = millis();
    Serial.println("Waktu NTP diperbarui: " + timeClient.getFormattedTime());
  }
}

unsigned long getTimestamp() {
  if (timeClient.isTimeSet()) {
    return timeClient.getEpochTime();
  }
  return 0;
}

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

  showMessage(F("NFC Error"), F("Check koneksi"));
  digitalWrite(STATUS_LED_PIN, LOW);  // LED nyala menandakan error
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings", HTTP_POST, handleSaveSettings);
  server.on("/restart", HTTP_GET, handleRestart);
  server.on("/info", HTTP_GET, handleSystemInfo);

  server.begin();
  Serial.println(F("Server web dimulai pada port 80"));
}

void handleRoot() {
  String html = "<html><head>";
  html += "<title>Smart Card Terminal</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;max-width:800px;margin:0 auto;}";
  html += "h1{color:#2c3e50;}";
  html += "a{display:inline-block;background:#3498db;color:white;padding:10px 15px;text-decoration:none;border-radius:4px;margin:5px 0;}";
  html += "</style></head><body>";
  html += "<h1>Smart Card Terminal</h1>";
  html += "<p>Status: " + String(isWiFiConnected() ? "Online" : "Offline") + "</p>";
  html += "<p>Mode: " + String(state.mode == MODE_PAYMENT ? "Pembayaran" : "Akses") + "</p>";
  html += "<a href='/settings'>Pengaturan</a> ";
  html += "<a href='/info'>Informasi Sistem</a> ";
  html += "<a href='/restart'>Restart</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSettings() {
  if (server.method() == HTTP_GET) {
    String html = "<html><head>";
    html += "<title>Pengaturan</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;max-width:800px;margin:0 auto;}";
    html += "h1{color:#2c3e50;}";
    html += "label{display:block;margin:15px 0 5px;}";
    html += "input{width:100%;padding:8px;box-sizing:border-box;}";
    html += "button{background:#3498db;color:white;border:none;padding:10px 15px;margin:15px 0;cursor:pointer;border-radius:4px;}";
    html += "a{display:inline-block;background:#95a5a6;color:white;padding:10px 15px;text-decoration:none;border-radius:4px;}";
    html += "</style></head><body>";
    html += "<h1>Pengaturan Terminal</h1>";
    html += "<form method='post' action='/settings'>";
    html += "<label for='api_url'>API Base URL:</label>";
    html += "<input type='text' id='api_url' name='api_url' value='" + apiConfig.baseUrl + "'>";
    html += "<label for='terminal_id'>Terminal ID:</label>";
    html += "<input type='text' id='terminal_id' name='terminal_id' value='" + apiConfig.terminalId + "'>";
    html += "<label for='api_token'>API Token:</label>";
    html += "<input type='text' id='api_token' name='api_token' value='" + apiConfig.apiToken + "'>";
    html += "<label for='payment_amount'>Jumlah Pembayaran Default:</label>";
    html += "<input type='number' id='payment_amount' name='payment_amount' value='" + String(state.paymentAmount) + "'>";
    html += "<button type='submit'>Simpan</button> ";
    html += "<a href='/'>Kembali</a>";
    html += "</form>";
    html += "</body></html>";

    server.send(200, "text/html", html);
  }
}

void handleSaveSettings() {
  if (server.hasArg("api_url")) apiConfig.baseUrl = server.arg("api_url");
  if (server.hasArg("terminal_id")) apiConfig.terminalId = server.arg("terminal_id");
  if (server.hasArg("api_token")) apiConfig.apiToken = server.arg("api_token");
  if (server.hasArg("payment_amount")) state.paymentAmount = server.arg("payment_amount").toFloat();

  saveConfig();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleRestart() {
  server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body><h1>Memulai Ulang...</h1><p>Tunggu sebentar...</p></body></html>");
  delay(1000);
  ESP.restart();
}

void handleSystemInfo() {
  String html = "<html><head>";
  html += "<title>Informasi Sistem</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;max-width:800px;margin:0 auto;}";
  html += "h1{color:#2c3e50;}";
  html += "table{width:100%;border-collapse:collapse;}";
  html += "th,td{text-align:left;padding:8px;border-bottom:1px solid #ddd;}";
  html += "tr:nth-child(even){background-color:#f2f2f2;}";
  html += "a{display:inline-block;background:#95a5a6;color:white;padding:10px 15px;text-decoration:none;border-radius:4px;margin-top:15px;}";
  html += "</style></head><body>";
  html += "<h1>Informasi Sistem</h1>";
  html += "<table>";
  html += "<tr><th>Item</th><th>Nilai</th></tr>";
  html += "<tr><td>Versi ESP8266</td><td>" + ESP.getCoreVersion() + "</td></tr>";
  html += "<tr><td>Frekuensi CPU</td><td>" + String(ESP.getCpuFreqMHz()) + " MHz</td></tr>";
  html += "<tr><td>Flash Chip Size</td><td>" + String(ESP.getFlashChipSize() / 1024) + " KB</td></tr>";
  html += "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap() / 1024) + " KB</td></tr>";
  html += "<tr><td>MAC Address</td><td>" + WiFi.macAddress() + "</td></tr>";
  html += "<tr><td>IP Address</td><td>" + WiFi.localIP().toString() + "</td></tr>";
  html += "<tr><td>SSID</td><td>" + WiFi.SSID() + "</td></tr>";
  html += "<tr><td>RSSI</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000 / 60) + " menit</td></tr>";
  html += "<tr><td>NFC Reader</td><td>PN532</td></tr>";

  // Terminal capabilities
  html += "<tr><td>Supports Payment</td><td>" + String(terminal.supports_payment ? "Ya" : "Tidak") + "</td></tr>";
  html += "<tr><td>Supports Access</td><td>" + String(terminal.supports_access ? "Ya" : "Tidak") + "</td></tr>";
  html += "<tr><td>Supports Topup</td><td>" + String(terminal.supports_topup ? "Ya" : "Tidak") + "</td></tr>";
  html += "<tr><td>Terminal Active</td><td>" + String(terminal.is_active ? "Ya" : "Tidak") + "</td></tr>";

  html += "</table>";
  html += "<a href='/'>Kembali</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

bool authenticateTerminal() {
  if (!isWiFiConnected()) {
    return false;
  }

  showMessage(F("Autentikasi"), F("Terminal..."));

  StaticJsonDocument<128> doc;
  doc["terminal_id"] = apiConfig.terminalId;

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
      Serial.println("Terminal capabilities:");
      Serial.println(" - Payment: " + String(terminal.supports_payment ? "Yes" : "No"));
      Serial.println(" - Topup: " + String(terminal.supports_topup ? "Yes" : "No"));
      Serial.println(" - Transfer: " + String(terminal.supports_transfer ? "Yes" : "No"));
      Serial.println(" - Access: " + String(terminal.supports_access ? "Yes" : "No"));

      Serial.println("Allowed user types:");
      for (int i = 0; i < terminal.allowed_types_count; i++) {
        Serial.println(" - " + terminal.allowed_user_types[i]);
      }

      if (terminal.is_active) {
        return true;
      } else {
        return false;
      }
    }
  }

  return false;
}

bool readCard() {
  if (millis() - state.lastCardReadTime < CARD_SCAN_DELAY) {
    return false;
  }

  uint8_t uidBuffer[7];
  uint8_t uidLength;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength, 100)) {
    return false;
  }

  if (uidLength == 0 || uidLength > 7) {
    return false;
  }

  String uid = "";
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

  if (uid == state.lastCardUID && (millis() - state.lastCardReadTime < 3000)) {
    return false;
  }

  state.cardUID = uid;
  state.lastCardUID = uid;
  state.lastCardReadTime = millis();

  Serial.println("Kartu terdeteksi: " + uid);
  showMessage(F("Kartu Terdeteksi"), uid.substring(0, 10) + "...");
  delay(800);

  return true;
}

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

void processCard() {
  state.transactionStatus = PROCESSING;
  state.transactionStartTime = millis();

  String userType;
  bool isUserActive;
  bool isBlocked;
  String cardInfo = getCardInfo(state.cardUID, isBlocked, userType, isUserActive);

  // Mode Pembayaran
  if (state.mode == MODE_PAYMENT) {
    // Check if terminal supports payment
    if (!terminal.supports_payment) {
      showMessage(F("Terminal Ini"), F("Tidak Support Payment"));
      delay(DISPLAY_TIME);
      state.transactionStatus = IDLE;
      updateDisplay();
      return;
    }

    showMessage(F("Memproses"), F("Pembayaran..."));
    if (processPayment(state.cardUID, state.paymentAmount)) {
      showMessage(F("Pembayaran Sukses"), F("Jumlah: ") + String(state.paymentAmount));
      state.transactionStatus = SUCCESS;

      // Display balance after successful payment
      delay(DISPLAY_TIME);
      String userType;
      bool isUserActive;
      bool isBlocked;
      String updatedCardInfo = getCardInfo(state.cardUID, isBlocked, userType, isUserActive);
      showMessage(F("Info Kartu"), updatedCardInfo);
    } else {
      showMessage(F("Pembayaran Gagal"), F("Coba lagi"));
      state.transactionStatus = FAILED;
    }
    delay(DISPLAY_TIME);
  }
  // Mode Akses
  else if (state.mode == MODE_ACCESS) {
    // Check if terminal supports access
    if (!terminal.supports_access) {
      showMessage(F("Terminal Ini"), F("Tidak Support Akses"));
      delay(DISPLAY_TIME);
      state.transactionStatus = IDLE;
      updateDisplay();
      return;
    }

    showMessage(F("Memverifikasi"), F("Akses..."));
    if (checkAccess(state.cardUID)) {
      showMessage(F("Akses Diizinkan"), F("Silakan masuk"));
      state.transactionStatus = SUCCESS;
      recordAccess(state.cardUID);

      // Display user info
      // Display user info
      delay(DISPLAY_TIME);
      showMessage(F("User: "), cardInfo);
    } else {
      showMessage(F("Akses Ditolak"), F("Hubungi admin"));
      state.transactionStatus = FAILED;
    }
    delay(DISPLAY_TIME);
  }

  // Kembalikan status ke IDLE
  state.transactionStatus = IDLE;
  updateDisplay();
}

void updateDisplay() {
  // Jika pesan yang akan ditampilkan sama dengan yang sudah ada, abaikan
  String newLine1, newLine2;

  switch (state.mode) {
    case MODE_PAYMENT:
      if (terminal.supports_payment) {
        newLine1 = F("Mode Pembayaran");
        newLine2 = F("Jumlah: ") + String(state.paymentAmount);
      } else {
        newLine1 = F("Mode Pembayaran");
        newLine2 = F("Tidak Support");
      }
      break;
    case MODE_ACCESS:
      if (terminal.supports_access) {
        newLine1 = F("Mode Akses");
        newLine2 = F("Tempel Kartu");
      } else {
        newLine1 = F("Mode Akses");
        newLine2 = F("Tidak Support");
      }
      break;
    case MODE_SETTINGS:
      newLine1 = F("Mode Pengaturan");
      newLine2 = F("Buka browser");
      break;
  }

  if (newLine1 != state.currentDisplayMsg[0] || newLine2 != state.currentDisplayMsg[1]) {
    showMessage(newLine1, newLine2, 0, false);
  }
}

void handleButton() {
  static bool lastButtonState = false;
  static unsigned long lastDebounceTime = 0;
  static bool debouncedButtonState = false;
  static bool lastDebouncedState = false;
  static unsigned long buttonPressStartTime = 0;

  bool currentButtonState = digitalRead(BUTTON_PIN) == LOW;

  // Debounce logika
  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();

    // Jika tombol baru saja ditekan, catat waktu awal
    if (currentButtonState) {
      buttonPressStartTime = millis();
    }
  }
  lastButtonState = currentButtonState;

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    debouncedButtonState = currentButtonState;

    // Deteksi tombol ditekan (falling edge)
    if (debouncedButtonState && !lastDebouncedState) {
      // Tidak ada tindakan pada tekan, hanya catat waktu mulai
    }

    // Deteksi tombol dilepas (rising edge)
    if (!debouncedButtonState && lastDebouncedState) {
      unsigned long pressDuration = millis() - buttonPressStartTime;

      // Tombol pendek: ganti mode operasi
      if (pressDuration < 2000) {
        Serial.println("Tombol terdeteksi - beralih mode");

        // Rotasi mode operasi
        switch (state.mode) {
          case MODE_PAYMENT:
            state.mode = MODE_ACCESS;
            break;
          case MODE_ACCESS:
            state.mode = MODE_PAYMENT;
            break;
          case MODE_SETTINGS:
            state.mode = MODE_PAYMENT;
            break;
        }

        updateDisplay();
      }
      // Tombol panjang: masuk ke mode pengaturan
      else if (pressDuration >= 2000) {
        Serial.println("Tombol panjang terdeteksi - mode pengaturan");
        state.mode = MODE_SETTINGS;
        showMessage(F("Mode Pengaturan"), WiFi.localIP().toString());
        delay(DISPLAY_TIME);
        updateDisplay();
      }
    }

    lastDebouncedState = debouncedButtonState;
  }
}

String getCardInfo(const String &uid, bool &isBlocked, String &userType, bool &isUserActive) {
  if (!isWiFiConnected()) {
    isBlocked = true;
    isUserActive = false;
    userType = "";
    return "No WiFi";
  }

  String response;
  int httpCode = sendRequest("/terminal/cards/info?card_uid=" + uid, "GET", "", &response);

  if (httpCode == 200) {
    StaticJsonDocument<512> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    if (!error && respDoc["success"].as<bool>()) {
      String userName = respDoc["data"]["user"]["name"].as<String>();
      userType = respDoc["data"]["user"]["role_type"].as<String>();
      float balance = respDoc["data"]["card"]["balance"];
      isBlocked = respDoc["data"]["card"]["is_blocked"];

      // Ambil status user aktif dari respons
      isUserActive = (respDoc["data"]["user"].containsKey("status") && respDoc["data"]["user"]["status"] == "active");

      if (isBlocked) {
        return userName + " [BLOKIR]";
      } else {
        return userName + " Rp." + String(balance, 0);
      }
    }
  }

  if (httpCode == 404) {
    showMessage(F("Kartu"), F("Tidak Terdaftar"));
    delay(1500);
  }

  isBlocked = true;  // Default ke blokir jika tidak bisa mendapatkan informasi
  isUserActive = false;
  userType = "";
  return "Error: " + String(httpCode);
}

bool processPayment(const String &uid, float amount) {
  if (!isWiFiConnected()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["terminal_id"] = apiConfig.terminalId;
  doc["card_uid"] = uid;
  doc["amount"] = amount;
  doc["notes"] = F("Pembayaran di terminal ") + apiConfig.terminalId;
  doc["timestamp"] = getTimestamp();

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

bool checkAccess(const String &uid) {
  if (!isWiFiConnected()) {
    return false;
  }

  StaticJsonDocument<192> doc;
  doc["terminal_id"] = apiConfig.terminalId;
  doc["card_uid"] = uid;
  doc["timestamp"] = getTimestamp();

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

void recordAccess(const String &uid) {
  if (!isWiFiConnected()) {
    return;
  }

  StaticJsonDocument<192> doc;
  doc["terminal_id"] = apiConfig.terminalId;
  doc["card_uid"] = uid;
  doc["notes"] = F("Akses di terminal ") + apiConfig.terminalId;
  doc["timestamp"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);

  sendRequest("/terminal/transactions/record-access", "POST", payload);
}

int sendRequest(const String &endpoint, const String &method, const String &data, String *response) {
  if (!isWiFiConnected()) {
    Serial.println(F("Tidak dapat mengirim permintaan: WiFi tidak terhubung"));
    return -1;
  }

  HTTPClient http;
  String url = apiConfig.baseUrl + endpoint;

  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + apiConfig.apiToken);
  http.addHeader("X-Device-ID", WiFi.macAddress());
  http.setTimeout(15000);

  int httpCode = -1;

  Serial.println("Mengirim " + method + " ke " + url);
  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(data);
  }

  if (httpCode > 0) {
    Serial.println("HTTP Status: " + String(httpCode));
    if (response != nullptr) {
      *response = http.getString();

      // Log respons yang diperpendek untuk debugging
      String shortResponse = *response;
      if (shortResponse.length() > 150) {
        shortResponse = shortResponse.substring(0, 150) + "...";
      }
      Serial.println("Respons: " + shortResponse);
    }
  } else {
    Serial.println("Error HTTP: " + http.errorToString(httpCode));
  }

  http.end();
  return httpCode;
}

void showMessage(const String &line1, const String &line2, int delayMs, bool log) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);

  if (line2.length() > 0) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }

  // Perbarui pesan saat ini untuk caching
  state.currentDisplayMsg[0] = line1;
  state.currentDisplayMsg[1] = line2;

  if (log) {
    Serial.println("LCD: " + line1 + " | " + line2);
  }

  if (delayMs > 0) {
    delay(delayMs);
  }
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED && state.isWiFiConnected;
}

void reconnectWiFi() {
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

    // Setelah terhubung kembali, update waktu NTP
    updateNtpTime();

    delay(1000);
    updateDisplay();
  } else {
    state.isWiFiConnected = false;
    showMessage(F("WiFi Gagal"), F("Coba lagi nanti"));
    delay(2000);
    updateDisplay();
  }
}