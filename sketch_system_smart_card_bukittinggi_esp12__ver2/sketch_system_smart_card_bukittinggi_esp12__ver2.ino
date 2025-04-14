#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ (15)
#define PN532_RESET (16)
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

#define BUTTON_PIN 0
int currentMode = 0;
unsigned long lastButtonPress = 0;
const int debounceDelay = 200;

const char *loginServerUrl = "http://smartcard.taibacreative.co.id/api/login";
const char *logServerURL = "http://smartcard.taibacreative.co.id/api/logs";
const char *logAccessTransportationServerURL = "http://smartcard.taibacreative.co.id/api/transaction/transports";
const char *logAccessGateServerURL = "http://smartcard.taibacreative.co.id/api/transaction/gates";
const char *logAccessClassServerURL = "http://smartcard.taibacreative.co.id/api/transaction/classes";

String uid;
String jwtToken = "";

// Inisialisasi LCD (I2C, alamat 0x27, ukuran 16x2)
LiquidCrystal_I2C lcd(0x27, 16, 2);
byte Check[8] = {
  0b00000,
  0b00001,
  0b00011,
  0b10110,
  0b11100,
  0b01000,
  0b00000,
  0b00000
};

void setup() {
  Serial.begin(115200);
  SPI.begin();
  nfc.begin();

  uint32_t versionPN532 = nfc.getFirmwareVersion();
  if (!versionPN532) {
    Serial.println("Error, versi RFID Reader PN532");
    while (1)
      ;
  }

  nfc.SAMConfig();

  lcd.init();
  lcd.backlight();
  lcd.createChar(0, Check);
  lcd.setCursor(0, 0);
  lcd.print("Selamat Datang");
  delay(2000);
  lcd.clear();

  WiFiManager wifiManager;

  wifiManager.resetSettings();

  if (!wifiManager.autoConnect("Taiba_smartcard", "password123")) {
    Serial.println("Gagal menghubungkan ke Wi-Fi, perangkat akan restart!");
    lcd.setCursor(0, 0);
    lcd.print("WiFi Error!");
    delay(2000);
    ESP.restart();
  }

  Serial.println("Wi-Fi Terhubung!");
  lcd.setCursor(0, 0);
  lcd.print("Wi-Fi Terhubung");
  delay(2000);
  lcd.clear();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  jwtToken = loginAndGetToken();
}

void loop() {
  if (millis() - lastButtonPress > debounceDelay) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      lastButtonPress = millis();
      currentMode = (currentMode + 1) % 4;

      lcd.clear();
      lcd.setCursor(0, 0);
      if (currentMode == 0) {
        lcd.print("Mode: Hanya");
        lcd.setCursor(0, 1);
        lcd.print("Log Aktivitas");
      } else if (currentMode == 1) {
        lcd.print("Mode:");
        lcd.setCursor(0, 1);
        lcd.print("Transportasi");
      } else if (currentMode == 2) {
        lcd.print("Mode: Gerbang");
      } else if (currentMode == 3) {
        lcd.print("Mode: Kelas");
      }
    }
  }

  uint8_t uidBuffer[7];
  uint8_t uidLength;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength, 50)) {
    return;
  }

  uid = "";
  for (byte i = 0; i < uidLength; i++) {
    uid += String(uidBuffer[i], HEX);
  }
  uid.toUpperCase();

  Serial.print("UID Terbaca: ");
  Serial.println(uid);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UID:");
  lcd.setCursor(0, 1);
  lcd.print(uid.substring(0, 8));  // Menampilkan maksimal 8 karakter UID
  delay(2000);

  String serverURL = logServerURL;  // Gunakan URL logServerURL untuk semua mode

  // Kirim request ke server berdasarkan mode yang dipilih
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    String logPayload = "{\"uid\": \"" + uid + "\"}";

    http.begin(client, logServerURL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + jwtToken);
    int httpLogCode = http.POST(logPayload);

    lcd.clear();
    lcd.setCursor(0, 0);
    if (httpLogCode > 0) {
      lcd.print("Log Berhasil");
    } else {
      lcd.print("Log Gagal");
    }
    delay(2000);
    http.end();
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Error");
    delay(2000);
  }

  String currentServerURLByMode = "";
  if (currentMode == 1) {
    currentServerURLByMode = logAccessTransportationServerURL;
  } else if (currentMode == 2) {
    currentServerURLByMode = logAccessGateServerURL;
  } else if (currentMode == 3) {
    currentServerURLByMode = logAccessClassServerURL;
  }
  // Serial.print(currentServerURLByMode);

  // Cek mode transportasi
  if (currentMode == 1 || currentMode == 2 || currentMode == 3) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      WiFiClient client;
      String finallyServerURL = currentServerURLByMode;
      String logPayload = "{\"uid\": \"" + uid + "\"}";

      http.begin(client, finallyServerURL);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Authorization", "Bearer " + jwtToken);
      int httpLogAccessCode = http.POST(logPayload);

      lcd.clear();
      lcd.setCursor(0, 0);
      if (httpLogAccessCode > 0) {
        String studentVerifyPayload = http.getString();
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, studentVerifyPayload);
        String message = doc["message"];

        Serial.print(message);

        if (message == "Check-in successful") {
          lcd.print("Jam Masuk");
          lcd.setCursor(0, 1);
          lcd.print("Tercatat");
          lcd.write(0);
        } else if (message == "Checkout successful") {
          lcd.print("Jam Pulang");
          lcd.setCursor(0, 1);
          lcd.print("Tercatat");
          lcd.write(0);
        } else if (message == "Checkout too early") {
          lcd.print("Jam Pulang");
          lcd.setCursor(0, 1);
          lcd.print("Terlalu Cepat");
        } else if (message == "Invalid card") {
          lcd.print("Maaf, Kartu");
          lcd.setCursor(0, 1);
          lcd.print("Tidak Valid");
        } else if (message == "Already checked out") {
          lcd.print("Jam Pulang");
          lcd.setCursor(0, 1);
          lcd.print("Sudah Tercatat");
          lcd.write(0);
        } else {
          lcd.print("Respon Tidak");
          lcd.setCursor(0, 1);
          lcd.print("Dikenal");
        }
        delay(3000);
      } else {
        lcd.print("Server Error");
        delay(2000);
      }
      http.end();
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Error");
      delay(2000);
    }
  }

  delay(3000);
}

String loginAndGetToken() {
  HTTPClient http;
  WiFiClient client;

  http.begin(client, loginServerUrl);
  http.addHeader("Content-Type", "application/json");

  String requestBody = "{\"email\":\"admin@gmail.com\",\"password\":\"admin123\"}";
  int httpResponseCode = http.POST(requestBody);

  String response = "";
  if (httpResponseCode > 0) {
    response = http.getString();
    Serial.println("Login Response: " + response);

    // Parse JSON untuk mendapatkan token
    DynamicJsonDocument doc(512);
    deserializeJson(doc, response);
    return doc["token"].as<String>();
  } else {
    Serial.println("Login Failed: " + String(httpResponseCode));
  }

  http.end();
  return "";
}