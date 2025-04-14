#include <WiFiManager.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>

// Pin RFID
#define SS_PIN 5
Adafruit_PN532 nfc(SS_PIN);

// Pin Tombol
#define BUTTON_PIN 12  // Misalnya tombol pada pin 12
int currentMode = 0;   // 0 = Mode Log, 1 = Mode Transport, 2 = Mode Gate, 3 = Mode Class

// Tambahan untuk debouncing tombol
unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 250; // 250ms debounce

// URL API
const char* logServerURL = "http://smartcard.taibacreative.co.id/api/logs/";
const char* logAccessTransportationServerURL = "http://smartcard.taibacreative.co.id/api/transaction/transports/";
const char* logAccessGateServerURL = "http://smartcard.taibacreative.co.id/api/transaction/gates/";
const char* logAccessClassServerURL = "http://smartcard.taibacreative.co.id/api/transaction/classes/";

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
  // Setup code remains the same
  Serial.begin(115200);
  SPI.begin();
  nfc.begin();

  if (!nfc.getFirmwareVersion()) {
    Serial.println("Tidak menemukan PN532!");
    while (1);
  }

  nfc.SAMConfig();

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
  wifiManager.resetSettings();

  if (!wifiManager.autoConnect("Taiba_WiFi_SmartCard_Configuration", "password123")) {
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
}

void loop() {
  // Cek tombol dengan debouncing
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long currentTime = millis();
    if (currentTime - lastButtonPress >= debounceTime) {
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
      lastButtonPress = currentTime;
      delay(500); // Mengurangi delay setelah tombol ditekan
    }
  }

  // Cek kartu RFID jika tidak sedang membaca
  if (!isReadingCard) {
    uint8_t uidBuffer[7];
    uint8_t uidLength;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength, 50)) {
      isReadingCard = true;
      
      // Proses pembacaan kartu
      processCard(uidBuffer, uidLength);
      
      delay(1000); // Mengurangi delay setelah membaca kartu
      isReadingCard = false;
    }
  }
}

void processCard(uint8_t* uidBuffer, uint8_t uidLength) {
  // Membuat string UID
  uid = "";
  for (byte i = 0; i < uidLength; i++) {
    uid += String(uidBuffer[i], HEX);
  }
  uid.toUpperCase();

  // Menampilkan UID
  Serial.print("UID Terbaca: ");
  Serial.println(uid);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UID:");
  lcd.setCursor(0, 1);
  lcd.print(uid.substring(0, 8));
  delay(1000); // Mengurangi delay tampilan UID

  // Proses pengiriman data ke server
  if (WiFi.status() == WL_CONNECTED) {
    // Log untuk semua mode
    HTTPClient http;
    String logPayload = "{\"uid\": \"" + uid + "\"}";
    
    http.begin(logServerURL);
    http.addHeader("Content-Type", "application/json");
    int httpLogCode = http.POST(logPayload);

    lcd.clear();
    lcd.setCursor(0, 0);
    if (httpLogCode > 0) {
      lcd.print("Log Berhasil");
    } else {
      lcd.print("Log Gagal");
    }
    delay(1000); // Mengurangi delay tampilan status log
    http.end();

    // Proses mode khusus
    if (currentMode > 0) {
      String currentServerURLByMode = "";
      if (currentMode == 1) {
        currentServerURLByMode = logAccessTransportationServerURL;
      } else if (currentMode == 2) {
        currentServerURLByMode = logAccessGateServerURL;
      } else if (currentMode == 3) {
        currentServerURLByMode = logAccessClassServerURL;
      }

      String fullServerURL = currentServerURLByMode + uid + "/verify";
      http.begin(fullServerURL);
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
        delay(2000); // Mengurangi delay tampilan status verifikasi
      } else {
        lcd.print("Server Error");
        delay(1000);
      }
      http.end();
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Error");
    delay(1000);
  }
}