#include <WiFiManager.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>

#define SDA_PIN 5   // SS / SEL0
#define RST_PIN 27  // RST Pin

Adafruit_PN532 nfc(SDA_PIN, RST_PIN);

const char* logServerURL = "http://smartcard.taibacreative.co.id/api/logs/";
const char* studentVerifyServerURL = "http://smartcard.taibacreative.co.id/api/students/";

String uid;
bool isReadingCard = false;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  nfc.begin();

  if (!nfc.getFirmwareVersion()) {
    Serial.println("Tidak menemukan PN532!");
    while (1);
  }
  
  nfc.SAMConfig();  // Konfigurasi untuk membaca kartu

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("RFID_Configurator", "password123")) {
    Serial.println("Gagal menghubungkan ke Wi-Fi, perangkat akan restart!");
    ESP.restart();
  }

  Serial.println("Wi-Fi Terhubung!");
}

void loop() {
  if (isReadingCard) return;

  uint8_t uidBuffer[7];  // UID bisa 4 atau 7 byte
  uint8_t uidLength;

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength)) {
    isReadingCard = true;

    uid = "";
    for (byte i = 0; i < uidLength; i++) {
      uid += String(uidBuffer[i], HEX);
    }
    uid.toUpperCase();

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

    // Kirim UID ke server menggunakan RESTful API
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

    delay(3000);
    isReadingCard = false;
  }
}
