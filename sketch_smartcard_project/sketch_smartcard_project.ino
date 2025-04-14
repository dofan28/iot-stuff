#include <WiFiManager.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>


#define RST_PIN 22
#define SS_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

const char* logServerURL = "http://smartcard.taibacreative.co.id/api/logs/";  // Endpoint untuk mencatat log
const char* studentVerifyServerURL = "http://smartcard.taibacreative.co.id/api/students/";  // Gunakan endpoint API untuk verifikasi
  
String uid;
bool isReadingCard = false;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;

  // Reset konfigurasi Wi-Fi sebelumnya jika diperlukan (opsional)
  // wifiManager.resetSettings();

  // Mulai WiFiManager
  if (!wifiManager.autoConnect("Project_SmartCard_By_Taiba", "password123")) {
    Serial.println("Gagal menghubungkan ke Wi-Fi, perangkat akan restart!");
    ESP.restart();  // Restart perangkat jika gagal koneksi
  }

  Serial.println("Wi-Fi Terhubung!");
}

void loop() {
  // Jika sedang memproses pembacaan kartu, keluar dari loop
  if (isReadingCard) return;

  // Periksa apakah kartu RFID didekatkan
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Tandai bahwa pembacaan kartu sedang berlangsung
  isReadingCard = true;

  // Baca UID kartu
  uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();  // Ubah UID menjadi huruf kapital

  Serial.print("UID Terbaca: ");
  Serial.println(uid);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String logPayload;

    // Kirim UID ke server untuk log
    http.begin(logServerURL);
    http.addHeader("Content-Type", "application/json");

    // Buat payload log
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
    String studentVerifyServerFullURL = String(studentVerifyServerURL) + uid + "/verify";  // Kirim UID sebagai parameter GET

    // Konfigurasi HTTP GET request
    http.begin(studentVerifyServerFullURL);

    int httpCode = http.GET();  // Mengirim permintaan GET

    // Proses respon dari server
    if (httpCode > 0) {
      String studentVerifyPayload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, studentVerifyPayload);
      String message = doc["message"];  // Ambil nilai dari key "message"
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

  // Delay untuk memastikan hanya satu pembacaan kartu yang diproses
  delay(3000);  // Delay 3 detik untuk menghindari pembacaan ganda

  // Reset status pembacaan kartu
  isReadingCard = false;
}
