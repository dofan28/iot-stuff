#include <WiFiManager.h>  // Library WiFiManager
#include <HTTPClient.h>   // Library HTTP
#include <SPI.h>
#include <MFRC522.h>

// Konfigurasi RFID
#define RST_PIN 22
#define SS_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

// URL server untuk validasi UID
const char* serverURL = "http://localhost:8000/students/";  // Ganti dengan URL server Anda

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;

  // Opsi: Reset konfigurasi Wi-Fi sebelumnya jika diperlukan
  wifiManager.resetSettings();

  // Mulai WiFiManager
  if (!wifiManager.autoConnect("RFID_Configurator", "password123")) {
    Serial.println("Gagal menghubungkan ke Wi-Fi, perangkat akan restart!");
    ESP.restart();  // Restart perangkat jika gagal koneksi
  }

  Serial.println("Wi-Fi Terhubung!");
}

void loop() {
  // Periksa apakah kartu RFID didekatkan
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Baca UID kartu
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();  // Ubah UID menjadi huruf kapital

  Serial.print("UID Terbaca: ");
  Serial.println(uid);

  // Kirim UID ke server untuk validasi
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String fullURL = String(serverURL) + uid + "/validated";  // Tambahkan UID sebagai parameter GET
    Serial.print("URL Akses: ");
    Serial.print(fullURL);
    http.begin(fullURL);        
    int httpCode = http.GET();  

    Serial.print(httpCode);

    if (httpCode > 0) {
      String payload = http.getString();  // Baca respon dari server
      Serial.println("Respon Server: " + payload);

      if (payload == "VALID") {
        Serial.println("Akses Diberikan!");
      } else {
        Serial.println("Akses Ditolak!");
      }
    } 
    else {
      Serial.println("Koneksi ke server gagal!");
    }

    http.end();  // Akhiri koneksi HTTP
  } else {
    Serial.println("Tidak terhubung ke Wi-Fi!");
  }

  delay(5000);  // Tunggu sebelum membaca kartu berikutnya
}
