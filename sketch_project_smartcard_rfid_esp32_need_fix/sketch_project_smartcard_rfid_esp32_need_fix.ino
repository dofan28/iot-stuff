#include <WiFiManager.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin RFID
#define RST_PIN 27
#define SS_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

// URL API
const char* logServerURL = "http://smartcard.taibacreative.co.id/api/logs/";
const char* studentVerifyServerURL = "http://smartcard.taibacreative.co.id/api/students/";

String uid;
bool isReadingCard = false;

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
  rfid.PCD_Init();

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, Check);
  lcd.setCursor(0, 0);
  lcd.print("Selamat Datang");  // Pesan awal
  delay(2000);
  lcd.clear();

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("Taiba_SmartCard_Configuration", "password123")) {
    Serial.println("Gagal menghubungkan ke Wi-Fi, perangkat akan restart!");
    lcd.setCursor(0, 0);
    lcd.print("WiFi Error!");
    delay(2000);
    ESP.restart();  // Restart perangkat jika gagal koneksi
  }

  Serial.println("Wi-Fi Terhubung!");
  lcd.setCursor(0, 0);
  lcd.print("Wi-Fi Terhubung");
  delay(2000);
  lcd.clear();
}

void loop() {
  if (isReadingCard) return;

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  isReadingCard = true;

  uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
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

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String logPayload;

    http.begin(logServerURL);
    http.addHeader("Content-Type", "application/json");
    logPayload = "{\"uid\": \"" + uid + "\"}";
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

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String studentVerifyServerFullURL = String(studentVerifyServerURL) + uid + "/verify";

    http.begin(studentVerifyServerFullURL);
    int httpCode = http.GET();

    lcd.clear();
    lcd.setCursor(0, 0);
    if (httpCode > 0) {
      String studentVerifyPayload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, studentVerifyPayload);
      String message = doc["message"];

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
  delay(3000);
  isReadingCard = false;
}
