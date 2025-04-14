#include <SPI.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ==== Konfigurasi Pin ====
#define PN532_IRQ (15)    // D8
#define PN532_RESET (16)  // D0
#define LED_SUCCESS (5)   // D1
#define LED_ERROR (4)     // D2
#define BUZZER_PIN (0)    // D3

// ==== Konfigurasi RFID PN532 ====
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// ==== Konfigurasi LCD I2C ====
LiquidCrystal_I2C lcd(0x27, 16, 2);  // alamat I2C LCD

// ==== Endpoint API ====
const char* serverUrl = "http://192.168.43.202:8000/api/admin/cards/card-detected";

// ==== Konfigurasi WiFi ====
const char* deviceName = "SmartCard-Setup";  // Nama hotspot saat setup
const unsigned long CHECK_WIFI_INTERVAL = 30000;  // Check WiFi setiap 30 detik
unsigned long lastWiFiCheck = 0;

// ==== Konfigurasi Timing ====
const unsigned long HTTP_TIMEOUT = 5000;    // Timeout HTTP request 5 detik
const unsigned long CARD_COOLDOWN = 1500;   // Cooldown setelah baca kartu 1.5 detik
const unsigned long DISPLAY_DELAY = 1500;   // Tampilkan pesan 1.5 detik
const unsigned long CARD_READ_TIMEOUT = 100; // Timeout pembacaan kartu 100ms
unsigned long lastCardRead = 0;

// ==== Status Sistem ====
struct {
  bool nfcInitialized = false;
  bool isReading = false;
  bool wifiConnected = false;
  String lastCardUID = "";
} state;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n===== Smart Card System Starting ====="));
  
  // Setup pins
  pinMode(LED_SUCCESS, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(LED_SUCCESS, LOW);
  digitalWrite(LED_ERROR, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Setup SPI dan I2C
  SPI.begin();
  Wire.begin();
  
  // Inisialisasi LCD
  setupLCD();
  
  // Setup WiFi
  setupWiFi();
  
  // Inisialisasi NFC Reader
  initializeNFC();
  
  // Sistem siap
  showMessage("System Ready", "Tap card");
  blinkLED(LED_SUCCESS, 2);
}

void loop() {
  // Cek koneksi WiFi periodik
  if (millis() - lastWiFiCheck > CHECK_WIFI_INTERVAL) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }
  
  // Skip pembacaan kartu jika sedang dalam cooldown atau sedang membaca
  if (millis() - lastCardRead < CARD_COOLDOWN || state.isReading) {
    yield(); // Beri kesempatan ESP8266 untuk handle background task
    return;
  }
  
  // Baca kartu RFID
  readCard();
  
  // Feed the watchdog
  ESP.wdtFeed();
}

void setupLCD() {
  lcd.init();
  lcd.backlight();
  showMessage("Smart Card", "Initializing...");
  delay(1000);
}

void setupWiFi() {
  showMessage("WiFi Setup", "Please wait...");
  
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);  // Portal timeout setelah 3 menit
  
  bool res = wm.autoConnect(deviceName);
  
  if (!res) {
    showMessage("WiFi Failed", "Restarting...");
    blinkLED(LED_ERROR, 3);
    beep(500);
    delay(2000);
    ESP.restart();
  } else {
    state.wifiConnected = true;
    showMessage("WiFi Connected", WiFi.localIP().toString());
    blinkLED(LED_SUCCESS, 2);
    beep(100);
    delay(DISPLAY_DELAY);
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    state.wifiConnected = false;
    showMessage("WiFi Disconnected", "Reconnecting...");
    WiFi.reconnect();
    
    // Tunggu koneksi dengan timeout
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 10) {
      delay(500);
      retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      state.wifiConnected = true;
      showMessage("WiFi Reconnected", WiFi.localIP().toString());
      blinkLED(LED_SUCCESS, 1);
      beep(100);
    } else {
      showMessage("WiFi Failed", "Check connection");
      blinkLED(LED_ERROR, 3);
      beep(500);
    }
    
    delay(DISPLAY_DELAY);
    showMessage("System Ready", "Tap card");
  } else {
    state.wifiConnected = true;
  }
}

void initializeNFC() {
  showMessage("Initializing", "NFC Reader...");
  
  nfc.begin();
  
  // Coba beberapa kali jika tidak terdeteksi di awal
  for (int attempt = 0; attempt < 3; attempt++) {
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
      // Berhasil mendeteksi PN532
      String version = String((versiondata >> 24) & 0xFF, HEX) + "." + 
                     String((versiondata >> 16) & 0xFF, DEC);
      
      showMessage("NFC Reader v" + version, "Detected");
      
      // Konfigurasi NFC reader
      nfc.SAMConfig();
      delay(100); // Delay setelah konfigurasi
      
      state.nfcInitialized = true;
      blinkLED(LED_SUCCESS, 2);
      beep(100);
      delay(DISPLAY_DELAY);
      return;
    }
    
    delay(500);
    Serial.println(F("Retrying NFC detection..."));
  }
  
  // Jika gagal setelah beberapa percobaan
  showMessage("NFC Reader Error", "Check connection");
  blinkLED(LED_ERROR, 5);
  beep(1000);
  state.nfcInitialized = false;
  delay(DISPLAY_DELAY);
}

void readCard() {
  // Coba inisialisasi ulang jika NFC belum siap
  if (!state.nfcInitialized) {
    initializeNFC();
    return;
  }
  
  uint8_t uid[7];  // UID dapat sampai 7 byte
  uint8_t uidLength;
  
  // Gunakan timeout yang lebih panjang untuk keandalan lebih baik
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, CARD_READ_TIMEOUT)) {
    state.isReading = true;
    lastCardRead = millis();
    
    // Konversi UID ke string
    String uidStr = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) {
        uidStr += "0";
      }
      uidStr += String(uid[i], HEX);
      if (i < uidLength - 1) {
        uidStr += ":";
      }
    }
    uidStr.toUpperCase();
    
    // Skip jika kartu yang sama (anti-duplikat)
    if (uidStr == state.lastCardUID && millis() - lastCardRead < 3000) {
      state.isReading = false;
      return;
    }
    
    state.lastCardUID = uidStr;
    
    // Tampilkan UID di LCD
    showMessage("Card Detected", uidStr.substring(0, 16));
    blinkLED(LED_SUCCESS, 1);
    beep(100);
    
    // Log ke serial
    Serial.println("Card UID: " + uidStr);
    
    // Kirim ke server
    sendCardToServer(uidStr);
    
    delay(1000); // Delay sebelum siap scan lagi
    showMessage("System Ready", "Tap card");
    state.isReading = false;
  }
}

void sendCardToServer(String uidStr) {
  if (WiFi.status() != WL_CONNECTED || !state.wifiConnected) {
    showMessage("WiFi Error", "Can't send data");
    blinkLED(LED_ERROR, 2);
    beep(300);
    return;
  }
  
  showMessage("Processing", "Please wait...");
  
  WiFiClient client;
  HTTPClient http;
  
  // Set timeout
  http.setTimeout(HTTP_TIMEOUT);
  
  // Start HTTP request
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "SmartCardReader/1.0");
  
  // Create JSON payload dengan buffer yang tepat
  StaticJsonDocument<200> doc;
  doc["uid"] = uidStr;
  doc["reader_id"] = WiFi.macAddress();
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  
  // Log request
  Serial.println("Sending to: " + String(serverUrl));
  Serial.println("Payload: " + payload);
  
  // Kirim request
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
      String response = http.getString();
      Serial.println("Response: " + response);
      
      // Parse respons JSON
      StaticJsonDocument<512> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);
      
      if (!error) {
        // Ambil data dari respons
        const char* message = responseDoc["message"];
        const char* name = responseDoc["name"];
        
        showMessage(message ? message : "Success", name ? name : "");
        blinkLED(LED_SUCCESS, 1);
        beep(200);
      } else {
        // Tampilkan response mentah jika parsing error
        showMessage("Success", response.substring(0, 16));
        blinkLED(LED_SUCCESS, 1);
        beep(100);
      }
    } else {
      showMessage("HTTP Error", String(httpCode));
      blinkLED(LED_ERROR, 2);
      beep(300);
    }
  } else {
    showMessage("Connection Failed", http.errorToString(httpCode));
    blinkLED(LED_ERROR, 3);
    beep(500);
  }
  
  http.end();
  delay(DISPLAY_DELAY);
}

void showMessage(String line1, String line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  
  if (line2.length() > 0) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }
}

void blinkLED(int pin, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(150);
    digitalWrite(pin, LOW);
    if (i < times - 1) {
      delay(150);
    }
  }
}

void beep(int duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}