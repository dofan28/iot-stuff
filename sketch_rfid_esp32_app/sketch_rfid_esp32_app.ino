#include <Wire.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>

#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);  // Inisialisasi PN532 dengan I2C

const char* logServerURL = "http://smartcard.taibacreative.co.id/api/logs/";
const char* studentVerifyServerURL = "http://smartcard.taibacreative.co.id/api/students/";

String uid;
bool isReadingCard = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  nfc.begin();

  // Cek apakah PN532 terdeteksi
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Gagal mendeteksi PN532!");
    while (1);
  }

  // Tampilkan versi firmware
  Serial.println("PN532 Terdeteksi!");
  nfc.SAMConfig();  // Konfigurasi PN532 untuk mode normal

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;

  if (!wifiManager.autoConnect("RFID_Configurator", "password123")) {
    Serial.println("Gagal menghubungkan ke Wi-Fi, restart!");
    ESP.restart();
  }

  Serial.println("Wi-Fi Terhubung!");
}

void loop() {
  if (isReadingCard) return;

  // Cek apakah ada kartu yang didekatkan
  uint8_t uidBuffer[7];  // Buffer untuk UID
  uint8_t uidLength;

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength)) {
    isReadingCard = true;

    uid = "";
    for (byte i = 0; i < uidLength; i++) {
      uid += String(uidBuffer[i], HEX);
    }
    uid.toUpperCase();  // Konversi ke huruf kapital

    Serial.print("UID Terbaca: ");
    Serial.println(uid);

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String logPayload;

      // Kirim UID ke server untuk log
      http.begin(logServerURL);
      http.addHeader("Content-Type", "application/json");

      logPayload = "{\"uid\": \"" + uid + "\"}";
      int httpLogCode = http.POST(logPayload);

      if (httpLogCode > 0) {
        Serial.println("Log UID berhasil disimpan!");
      } else {
        Serial.println("Gagal mencatat log UID!");
      }

      http.end();
    } else {
      Serial.println("Tidak terhubung ke Wi-Fi untuk mencatat log!");
    }

    // Kirim UID ke server menggunakan REST API
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String studentVerifyServerFullURL = String(studentVerifyServerURL) + uid + "/verify";

      http.begin(studentVerifyServerFullURL);
      int httpCode = http.GET();

      if (httpCode > 0) {
        String studentVerifyPayload = http.getString();
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, studentVerifyPayload);
        String message = doc["message"];
        Serial.println(message);

        if (message == "Check-in successful") {
          Serial.println("Jam Masuk Dicatat ✅");
        } else if (message == "Checkout successful") {
          Serial.println("Jam Pulang Dicatat ✅");
        } else if (message == "Checkout too early") {
          Serial.println("Gagal: Belum 10 menit sejak check-in ❌");
        } else if (message == "Invalid card") {
          Serial.println("Gagal: Kartu Tidak Terdaftar ❌");
        } else if (message == "Already checked out") {
          Serial.println("Jam Pulang Sudah Tercatat ✅");
        } else {
          Serial.println("Respon Tidak Dikenal: " + message);
        }
      } else {
        Serial.println("Koneksi ke server gagal!");
      }

      http.end();
    } else {
      Serial.println("Tidak terhubung ke Wi-Fi!");
    }

    // Delay 3 detik untuk menghindari pembacaan ganda
    delay(3000);
    isReadingCard = false;
  }
}
