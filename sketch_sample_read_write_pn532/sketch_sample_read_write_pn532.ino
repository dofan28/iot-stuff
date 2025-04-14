#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <ESP_Mail_Client.h>

// Konfigurasi PN532 untuk SPI
#define SS_PIN 5
Adafruit_PN532 nfc(SS_PIN);

void setup() {
    Serial.begin(115200);
    Serial.println("Inisialisasi PN532...");

    nfc.begin();
    
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("Tidak dapat mendeteksi PN532. Periksa koneksi!");
        while (1);
    }

    Serial.print("Ditemukan chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
    Serial.print("Firmware versi: "); Serial.print((versiondata>>16) & 0xFF, DEC);
    Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);

    nfc.SAMConfig();  // Konfigurasi PN532 untuk komunikasi RFID
    Serial.println("Menunggu kartu RFID/NFC...");
}

void loop() {
    Serial.println("\nPilih Mode: [1] Write | [2] Read");
    while (Serial.available() == 0);  // Tunggu input dari Serial Monitor
    char mode = Serial.read();

    if (mode == '1') {
        writeRFID();
    } else if (mode == '2') {
        readRFID();
    } else {
        Serial.println("Pilihan tidak valid!");
    }
}

// 📝 Fungsi Menulis Data ke Kartu RFID
void writeRFID() {
    Serial.println("Dekatkan kartu untuk menulis data...");
    
    uint8_t uid[7];
    uint8_t uidLength;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
        Serial.println("Gagal mendeteksi kartu!");
        return;
    }

    Serial.print("UID Kartu: ");
    for (uint8_t i = 0; i < uidLength; i++) {
        Serial.print(uid[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    uint8_t dataBlock[16] = { 'H', 'e', 'l', 'l', 'o', ' ', 'E', 'S', 'P', '3', '2', '!', ' ', ' ', ' ', ' ' };

    // Gunakan blok 4 untuk menyimpan data
    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})) {
        Serial.println("Autentikasi gagal! Cek kunci akses.");
        return;
    }

    if (nfc.mifareclassic_WriteDataBlock(4, dataBlock)) {
        Serial.println("Sukses menulis ke kartu!");
    } else {
        Serial.println("Gagal menulis ke kartu!");
    }
}

// 📖 Fungsi Membaca Data dari Kartu RFID
void readRFID() {
    Serial.println("Dekatkan kartu untuk membaca data...");

    uint8_t uid[7];
    uint8_t uidLength;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
        Serial.println("Gagal mendeteksi kartu!");
        return;
    }

    Serial.print("UID Kartu: ");
    for (uint8_t i = 0; i < uidLength; i++) {
        Serial.print(uid[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    uint8_t dataBlock[16];

    // Gunakan blok 4 untuk membaca data
    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})) {
        Serial.println("Autentikasi gagal! Tidak dapat membaca data.");
        return;
    }

    if (nfc.mifareclassic_ReadDataBlock(4, dataBlock)) {
        Serial.print("Data dari kartu: ");
        for (int i = 0; i < 16; i++) {
            Serial.write(dataBlock[i]);  // Menampilkan data sebagai karakter
        }
        Serial.println();
    } else {
        Serial.println("Gagal membaca dari kartu!");
    }
}
