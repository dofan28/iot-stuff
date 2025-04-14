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

// Pin definitions - tetap sama seperti kode asli
#define PN532_IRQ 15
#define PN532_RESET 16
#define BUTTON_PIN 0
#define LED_PIN 2
#define BUZZER_PIN 5

// Konfigurasi API
const char *API_BASE_URL = "http://192.168.43.202:8000/api";
const char *TERMINAL_ID = "1";
const char *API_TOKEN = "1234567890abcdef";

// Konstanta waktu yang dioptimalkan
#define CARD_SCAN_DELAY 500  // Delay minimal antar scan (ms)
#define DEBOUNCE_DELAY 150   // Debounce tombol (ms)
#define DISPLAY_TIME 1500    // Waktu menampilkan pesan (ms)
#define RETRY_DELAY 3000     // Delay sebelum mencoba lagi koneksi (ms)

// Mode operasi
enum OperationMode {
  MODE_IDLE = 0,
  MODE_PAYMENT = 1,
  MODE_TOPUP = 2,
  MODE_ACCESS = 3
};

// Objek global
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiClient;

// Variabel status
struct {
  OperationMode mode = MODE_IDLE;
  float paymentAmount = 5000.0;
  float topupAmount = 10000.0;
  String cardUID;
  String lastCardUID;
  unsigned long lastCardReadTime = 0;
  unsigned long lastButtonPressTime = 0;
  bool buttonPressed = false;
  bool isProcessing = false;
  bool isWiFiConnected = false;
} state;

// Kemampuan terminal
struct {
  bool payment = true;
  bool topup = true;
  bool access = true;
} capabilities;

// Fungsi forward declarations
void setupWiFi();
void setupNFC();
void setupLCD();
bool authenticateTerminal();
bool readCard();
void processCard();
void updateDisplay();
void handleButton();
void showMessage(const String &line1, const String &line2 = "", int delayMs = 0);
void blinkLED(int times, int delayMs = 100);
void beepSuccess();
void beepError();
bool isWiFiConnected();
void reconnectWiFi();
int sendRequest(const String &endpoint, const String &method, const String &data, String *response = nullptr);

// Fungsi-fungsi transaksi
String getCardInfo(const String &uid);
bool processPayment(const String &uid, float amount);
bool processTopup(const String &uid, float amount);
bool checkAccess(const String &uid);
void recordAccess(const String &uid);

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== Smart Card Terminal System ==="));

  // Setup pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // OFF (active LOW)
  digitalWrite(BUZZER_PIN, LOW);

  // Inisialisasi
  setupLCD();
  setupWiFi();
  setupNFC();

  if (authenticateTerminal()) {
    showMessage(F("Terminal Siap"), F("Tempel Kartu"));
  } else {
    showMessage(F("Auth Gagal"), F("Restart sistem"));
    while (1) {
      blinkLED(3, 200);
      delay(1000);
      ESP.wdtFeed();
    }
  }
}

void loop() {
  // Cek koneksi WiFi periodik
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 10000) {  // Cek setiap 10 detik
    lastWifiCheck = millis();
    if (!isWiFiConnected()) {
      reconnectWiFi();
    }
  }

  // Cek tombol
  handleButton();

  // Baca kartu jika tidak sedang memproses
  if (!state.isProcessing) {
    if (readCard()) {
      processCard();
    }
  }

  // Feed watchdog
  ESP.wdtFeed();
  yield();
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

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);  // 3 menit timeout

  // Gunakan SSID dan password yang tersimpan
  if (!wifiManager.autoConnect("SmartCard_AP", "password123")) {
    showMessage(F("WiFi Gagal"), F("Restart..."));
    delay(3000);
    ESP.restart();
  }

  state.isWiFiConnected = true;
  showMessage(F("WiFi Terhubung"), WiFi.localIP().toString());
  delay(1000);
}

void setupNFC() {
  showMessage(F("NFC Reader"), F("Inisialisasi..."));

  // Inisialisasi NFC reader
  nfc.begin();

  // Pastikan SPI terhubung
  SPI.begin();

  // Verifikasi koneksi reader
  for (int attempt = 0; attempt < 3; attempt++) {
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
      // Tampilkan versi firmware
      char versionStr[10];
      sprintf(versionStr, "%d.%d", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
      showMessage(F("RFID Reader OK"), F("Versi: ") + String(versionStr));

      // Konfigurasi NFC reader dengan timeout lebih panjang
      nfc.SAMConfig();
      delay(1000);
      return;
    }
    delay(500);
  }

  // Jika gagal mendeteksi reader
  showMessage(F("NFC Error"), F("Reader tidak terdeteksi"));
  while (1) {
    blinkLED(5, 100);
    delay(2000);
    ESP.wdtFeed();
  }
}

bool authenticateTerminal() {
  if (!isWiFiConnected()) {
    return false;
  }

  showMessage(F("Autentikasi"), F("Terminal..."));

  StaticJsonDocument<128> doc;
  doc["terminal_id"] = TERMINAL_ID;

  String payload;
  serializeJson(doc, payload);

  String response;
  int httpCode = sendRequest("/terminal/authenticate", "POST", payload, &response);

  if (httpCode == 200) {
    StaticJsonDocument<256> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    if (!error && respDoc["success"].as<bool>()) {
      // Set terminal capabilities
      JsonObject data = respDoc["data"]["terminal"];
      capabilities.payment = data["supports_payment"];
      capabilities.topup = data["supports_topup"];
      capabilities.access = data["supports_access"];
      return true;
    }
  }

  return false;
}

bool readCard() {
  // Perhatikan interval pembacaan kartu
  if (millis() - state.lastCardReadTime < CARD_SCAN_DELAY) {
    return false;
  }

  uint8_t uidBuffer[7];
  uint8_t uidLength;

  // Baca kartu dengan timeout lebih lama (50ms)
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength, 100)) {
    return false;
  }

  // Validasi hasil pembacaan
  if (uidLength == 0 || uidLength > 7) {
    return false;
  }

  // Konversi buffer UID ke string
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

  // Cek apakah kartu yang sama dengan pembacaan terakhir
  if (uid == state.lastCardUID && (millis() - state.lastCardReadTime < 3000)) {
    return false;
  }

  // Update status
  state.cardUID = uid;
  state.lastCardUID = uid;
  state.lastCardReadTime = millis();

  Serial.println("Kartu terdeteksi: " + uid);
  showMessage(F("Kartu Terdeteksi"), uid.substring(0, 10) + "...");
  beepSuccess();
  delay(800);

  return true;
}

void processCard() {
  state.isProcessing = true;

  switch (state.mode) {
    case MODE_IDLE:
      {
        String cardInfo = getCardInfo(state.cardUID);
        if (cardInfo.length() > 0) {
          showMessage(F("Info Kartu"), cardInfo);
        } else {
          showMessage(F("Kartu Tidak Dikenal"), state.cardUID.substring(0, 8) + "...");
          beepError();
        }
        delay(DISPLAY_TIME);
      }
      break;

    case MODE_PAYMENT:
      showMessage(F("Memproses"), F("Pembayaran..."));
      if (processPayment(state.cardUID, state.paymentAmount)) {
        showMessage(F("Pembayaran Sukses"), F("Jumlah: ") + String(state.paymentAmount));
        beepSuccess();
        blinkLED(2, 200);
      } else {
        showMessage(F("Pembayaran Gagal"), F("Coba lagi"));
        beepError();
        blinkLED(3, 200);
      }
      delay(DISPLAY_TIME);
      state.mode = MODE_IDLE;
      break;

    case MODE_TOPUP:
      showMessage(F("Memproses"), F("Top Up..."));
      if (processTopup(state.cardUID, state.topupAmount)) {
        showMessage(F("Top Up Sukses"), F("Jumlah: ") + String(state.topupAmount));
        beepSuccess();
        blinkLED(2, 200);
      } else {
        showMessage(F("Top Up Gagal"), F("Coba lagi"));
        beepError();
        blinkLED(3, 200);
      }
      delay(DISPLAY_TIME);
      state.mode = MODE_IDLE;
      break;

    case MODE_ACCESS:
      showMessage(F("Memverifikasi"), F("Akses..."));
      if (checkAccess(state.cardUID)) {
        showMessage(F("Akses Diizinkan"), F("Silakan masuk"));
        beepSuccess();
        blinkLED(2, 200);
        recordAccess(state.cardUID);
      } else {
        showMessage(F("Akses Ditolak"), F("Hubungi admin"));
        beepError();
        blinkLED(3, 200);
      }
      delay(DISPLAY_TIME);
      state.mode = MODE_IDLE;
      break;
  }

  state.isProcessing = false;
  updateDisplay();
}

void updateDisplay() {
  switch (state.mode) {
    case MODE_IDLE:
      showMessage(F("Terminal Siap"), F("Tempel Kartu"));
      break;
    case MODE_PAYMENT:
      showMessage(F("Mode Pembayaran"), F("Jumlah: ") + String(state.paymentAmount));
      break;
    case MODE_TOPUP:
      showMessage(F("Mode Top Up"), F("Jumlah: ") + String(state.topupAmount));
      break;
    case MODE_ACCESS:
      showMessage(F("Mode Akses"), F("Tempel Kartu"));
      break;
  }
}

void handleButton() {
  bool buttonState = digitalRead(BUTTON_PIN) == LOW;

  if (buttonState && !state.buttonPressed) {
    state.buttonPressed = true;
    state.lastButtonPressTime = millis();
  } else if (buttonState && state.buttonPressed && (millis() - state.lastButtonPressTime > DEBOUNCE_DELAY)) {
    // Ganti mode
    switch (state.mode) {
      case MODE_IDLE:
        if (capabilities.payment) {
          state.mode = MODE_PAYMENT;
        } else if (capabilities.topup) {
          state.mode = MODE_TOPUP;
        } else if (capabilities.access) {
          state.mode = MODE_ACCESS;
        }
        break;

      case MODE_PAYMENT:
        if (capabilities.topup) {
          state.mode = MODE_TOPUP;
        } else if (capabilities.access) {
          state.mode = MODE_ACCESS;
        } else {
          state.mode = MODE_IDLE;
        }
        break;

      case MODE_TOPUP:
        if (capabilities.access) {
          state.mode = MODE_ACCESS;
        } else {
          state.mode = MODE_IDLE;
        }
        break;

      case MODE_ACCESS:
        state.mode = MODE_IDLE;
        break;
    }

    beepSuccess();
    updateDisplay();
    state.lastButtonPressTime = millis() + 500;  // Prevent multiple triggers
  } else if (!buttonState && state.buttonPressed) {
    state.buttonPressed = false;
  }
}

String getCardInfo(const String &uid) {
  if (!isWiFiConnected()) {
    return "No WiFi";
  }

  String url = String(API_BASE_URL) + "/terminal/cards/info?card_uid=" + uid;

  String response;
  int httpCode = sendRequest("/terminal/cards/info?card_uid=" + uid, "GET", "", &response);

  if (httpCode == 200) {
    StaticJsonDocument<384> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    if (!error && respDoc["success"].as<bool>()) {
      String userName = respDoc["data"]["user"]["name"].as<String>();
      float balance = respDoc["data"]["card"]["balance"];
      bool isBlocked = respDoc["data"]["card"]["is_blocked"];

      if (isBlocked) {
        return userName + " [BLOKIR]";
      } else {
        return userName + " Rp." + String(balance, 0);
      }
    }
  }

  return "";
}

bool processPayment(const String &uid, float amount) {
  if (!isWiFiConnected()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;
  doc["amount"] = amount;
  doc["notes"] = F("Pembayaran di terminal ") + String(TERMINAL_ID);

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

bool processTopup(const String &uid, float amount) {
  if (!isWiFiConnected()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;
  doc["amount"] = amount;
  doc["notes"] = F("Top up di terminal ") + String(TERMINAL_ID);

  String payload;
  serializeJson(doc, payload);

  String response;
  int httpCode = sendRequest("/terminal/process-topup", "POST", payload, &response);

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

  StaticJsonDocument<128> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;

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

  StaticJsonDocument<128> doc;
  doc["terminal_id"] = TERMINAL_ID;
  doc["card_uid"] = uid;
  doc["notes"] = F("Akses di terminal ") + String(TERMINAL_ID);

  String payload;
  serializeJson(doc, payload);

  sendRequest("/terminal/transactions/record-access", "POST", payload);
}

int sendRequest(const String &endpoint, const String &method, const String &data, String *response) {
  if (!isWiFiConnected()) {
    return -1;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + endpoint;

  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(API_TOKEN));
  http.setTimeout(8000);  // 8 detik timeout

  int httpCode = -1;

  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(data);
  }

  if (httpCode > 0 && response != nullptr) {
    *response = http.getString();
  }

  http.end();
  return httpCode;
}

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

void beepSuccess() {
  tone(BUZZER_PIN, 2000, 80);
  delay(100);
  tone(BUZZER_PIN, 2500, 120);
}

void beepError() {
  tone(BUZZER_PIN, 350, 180);
  delay(200);
  tone(BUZZER_PIN, 350, 180);
}

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);  // LED on (active LOW)
    delay(delayMs);
    digitalWrite(LED_PIN, HIGH);  // LED off
    if (i < times - 1) {
      delay(delayMs);
    }
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
    delay(1000);
    updateDisplay();
  } else {
    state.isWiFiConnected = false;
    showMessage(F("WiFi Gagal"), F("Coba lagi nanti"));
    delay(2000);
    updateDisplay();
  }
}