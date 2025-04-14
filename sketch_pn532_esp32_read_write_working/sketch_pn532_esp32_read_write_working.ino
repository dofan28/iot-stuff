#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// Konfigurasi pin koneksi PN532 dengan ESP32
#define PN532_SCK  (18)
#define PN532_MOSI (23)
#define PN532_SS   (5)
#define PN532_MISO (19)

// Buat instance PN532 menggunakan koneksi SPI
Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

// Teks yang akan ditulis ke kartu
const char textToWrite[] = "Halo Indonesia";
uint8_t dataToWrite[16]; // Buffer 16 byte untuk data yang akan ditulis

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 + PN532 RFID Read/Write Program (Bahasa Indonesia)");
  
  nfc.begin();
  
  // Tunggu hingga PN532 tersedia
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Tidak dapat menemukan board PN532!");
    while (1); // Berhenti
  }
  
  // Menampilkan informasi firmware PN532
  Serial.print("Ditemukan chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // Konfigurasi PN532 untuk membaca kartu RFID
  nfc.SAMConfig();
  
  // Siapkan data untuk ditulis ke kartu
  prepareTextData();
  
  Serial.println("Siap membaca dan menulis tag RFID...");
  Serial.println("Tekan 'R' untuk membaca data");
  Serial.println("Tekan 'W' untuk menulis teks: \"" + String(textToWrite) + "\"");
}

void loop() {
  // Periksa jika ada perintah dari Serial Monitor
  if (Serial.available() > 0) {
    char command = Serial.read();
    
    if (command == 'R' || command == 'r') {
      readCard();
    } 
    else if (command == 'W' || command == 'w') {
      writeCard();
    } 
    else if (command == 'T' || command == 't') {
      // Fitur tambahan untuk mengubah teks
      Serial.println("Masukkan teks baru (maksimal 16 karakter):");
      String newText = "";
      while (true) {
        if (Serial.available()) {
          char c = Serial.read();
          if (c == '\n') {
            break;
          }
          newText += c;
        }
      }
      updateTextToWrite(newText);
    }
  }
  
  delay(100);
}

// Fungsi untuk menyiapkan data teks ke dalam format yang dapat ditulis ke kartu
void prepareTextData() {
  // Reset buffer data terlebih dahulu
  memset(dataToWrite, 0, sizeof(dataToWrite));
  
  // Salin teks ke buffer data (maksimum 16 byte)
  int textLength = strlen(textToWrite);
  if (textLength > 16) textLength = 16;
  
  memcpy(dataToWrite, textToWrite, textLength);
  
  Serial.println("Data teks siap ditulis:");
  for (int i = 0; i < 16; i++) {
    Serial.print(" 0x"); Serial.print(dataToWrite[i], HEX);
  }
  Serial.println("");
}

// Fungsi untuk mengubah teks yang akan ditulis
void updateTextToWrite(String newText) {
  if (newText.length() > 16) {
    Serial.println("Teks terlalu panjang, dipotong menjadi 16 karakter.");
    newText = newText.substring(0, 16);
  }
  
  // Reset buffer data
  memset(dataToWrite, 0, sizeof(dataToWrite));
  
  // Salin teks baru ke buffer data
  newText.getBytes(dataToWrite, newText.length() + 1);
  
  Serial.println("Teks baru: \"" + newText + "\"");
  Serial.println("Data teks siap ditulis:");
  for (int i = 0; i < 16; i++) {
    Serial.print(" 0x"); Serial.print(dataToWrite[i], HEX);
  }
  Serial.println("");
}

void readCard() {
  Serial.println("Menunggu kartu RFID...");
  
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer untuk menyimpan UID kartu
  uint8_t uidLength;                         // Panjang UID (4 atau 7 byte)
  
  // Tunggu kartu RFID hadir (blocking)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success) {
    Serial.println("Kartu terdeteksi!");
    Serial.print("UID Length: "); Serial.print(uidLength, DEC); Serial.println(" bytes");
    Serial.print("UID Value: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(" 0x"); Serial.print(uid[i], HEX);
    }
    Serial.println("");
    
    // Baca data dari blok 4 (blok 0-3 biasanya merupakan sektor sistem)
    uint8_t blockNumber = 4;
    uint8_t data[16];
    uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // Kunci default

    // Autentikasi blok 4
    success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNumber, 0, keya);
    
    if (success) {
      Serial.println("Autentikasi berhasil.");
      
      // Baca data dari blok
      success = nfc.mifareclassic_ReadDataBlock(blockNumber, data);
      
      if (success) {
        Serial.println("Data berhasil dibaca:");
        Serial.print("Blok "); Serial.print(blockNumber); Serial.println(":");
        
        // Tampilkan data dalam format hex
        for (uint8_t i = 0; i < 16; i++) {
          Serial.print(" 0x"); Serial.print(data[i], HEX);
        }
        Serial.println("");
        
        // Tampilkan data sebagai teks
        Serial.print("Teks: \"");
        char textBuffer[17]; // 16 byte + null terminator
        memset(textBuffer, 0, sizeof(textBuffer));
        memcpy(textBuffer, data, 16);
        Serial.print(textBuffer);
        Serial.println("\"");
      } else {
        Serial.println("Error! Gagal membaca data dari blok.");
      }
    } else {
      Serial.println("Error! Autentikasi gagal.");
    }
  } else {
    Serial.println("Waktu tunggu habis, tidak ada kartu terdeteksi.");
  }
}

void writeCard() {
  Serial.println("Dekatkan kartu RFID untuk ditulis...");
  
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer untuk menyimpan UID kartu
  uint8_t uidLength;                         // Panjang UID (4 atau 7 byte)
  
  // Tunggu kartu RFID hadir (blocking)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success) {
    Serial.println("Kartu terdeteksi!");
    Serial.print("UID Length: "); Serial.print(uidLength, DEC); Serial.println(" bytes");
    Serial.print("UID Value: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(" 0x"); Serial.print(uid[i], HEX);
    }
    Serial.println("");
    
    // Tulis data ke blok 4
    uint8_t blockNumber = 4;
    uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // Kunci default

    // Autentikasi blok 4
    success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNumber, 0, keya);
    
    if (success) {
      Serial.println("Autentikasi berhasil.");
      
      // Tulis data ke blok
      success = nfc.mifareclassic_WriteDataBlock(blockNumber, dataToWrite);
      
      if (success) {
        Serial.println("Data berhasil ditulis!");
        Serial.print("Blok "); Serial.print(blockNumber); Serial.println(" telah diisi dengan teks:");
        Serial.println("\"" + String((char*)dataToWrite) + "\"");
      } else {
        Serial.println("Error! Gagal menulis data ke blok.");
      }
    } else {
      Serial.println("Error! Autentikasi gagal.");
    }
  } else {
    Serial.println("Waktu tunggu habis, tidak ada kartu terdeteksi.");
  }
}