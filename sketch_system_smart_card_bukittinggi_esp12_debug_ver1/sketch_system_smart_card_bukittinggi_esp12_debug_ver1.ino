/**
 * SmartEduCard System - Optimized
 * 
 * Sistem RFID untuk pencatatan aktivitas pendidikan
 * Versi: 2.1
 */

#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>

// Pin konfigurasi
#define PN532_IRQ (15)
#define PN532_RESET (16)
#define BUTTON_PIN (0)
#define LED_PIN (2)  // LED bawaan ESP8266 (active LOW)

// Konstanta waktu - dioptimasi menjadi lebih singkat
#define DEBOUNCE_DELAY 150      // ms (dari 200ms)
#define DELAY_SHORT 800         // ms (dari 1500ms)
#define DELAY_LONG 1500         // ms (dari 2500ms)
#define TOKEN_LIFETIME 3600000  // 1 jam dalam ms
#define CARD_SCAN_DELAY 500     // ms (dari 800ms) - delay minimal antar scan kartu yang sama

// Mode operasi
enum OperationMode {
  MODE_LOG = 0,
  MODE_TRANS = 1,
  MODE_GATE = 2,
  MODE_CLASS = 3,
  MODE_COUNT = 4
};

// Nama mode untuk display - simpan dalam Flash memory dengan F()
const char MODE_LOG_STR[] PROGMEM = "Log Aktivitas";
const char MODE_TRANS_STR[] PROGMEM = "Transportasi";
const char MODE_GATE_STR[] PROGMEM = "Gerbang Sekolah";
const char MODE_CLASS_STR[] PROGMEM = "Kelas";

// URL Server - simpan dalam Flash memory
const char LOGIN_URL[] PROGMEM = "http://smartcard.taibacreative.co.id/api/login";
const char LOG_URL[] PROGMEM = "http://smartcard.taibacreative.co.id/api/logs";
const char TRANS_URL[] PROGMEM = "http://smartcard.taibacreative.co.id/api/transaction/transports";
const char GATE_URL[] PROGMEM = "http://smartcard.taibacreative.co.id/api/transaction/gates";
const char CLASS_URL[] PROGMEM = "http://smartcard.taibacreative.co.id/api/transaction/classes";

// Kode respons kustom
enum ResponseCode {
  RESP_WIFI_ERROR = -1,
  RESP_TOKEN_ERROR = -2,
  RESP_JSON_ERROR = -3,
  RESP_TIMEOUT = -4
};

// Objek global
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Variabel global
String jwtToken;
OperationMode currentMode = MODE_LOG;
unsigned long lastButtonPress = 0;
unsigned long lastCardRead = 0;
unsigned long tokenTimestamp = 0;
String lastCardUID;
bool systemReady = false;
WiFiClient wifiClient;  // Gunakan satu objek WiFiClient untuk semua koneksi

// Karakter khusus
byte checkChar[8] = {
  0b00000, 0b00001, 0b00011,
  0b10110, 0b11100, 0b01000,
  0b00000, 0b00000
};

/**
 * Fungsi utilitas untuk mengambil string dari PROGMEM
 */
String getFlashString(const char* flashStr) {
  return String(FPSTR(flashStr));
}

/**
 * Mendapatkan nama mode dari PROGMEM
 */
String getModeName(OperationMode mode) {
  switch (mode) {
    case MODE_LOG: return getFlashString(MODE_LOG_STR);
    case MODE_TRANS: return getFlashString(MODE_TRANS_STR);
    case MODE_GATE: return getFlashString(MODE_GATE_STR);
    case MODE_CLASS: return getFlashString(MODE_CLASS_STR);
    default: return F("Mode Error");
  }
}

/**
 * Tampilkan pesan di LCD dengan baris opsional
 */
void showMessage(const String& line1, const String& line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);

  if (line2.length() > 0) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }
}

/**
 * Tampilkan pesan sukses dengan centang
 */
void showSuccess(const String& line1, const String& line2 = "") {
  showMessage(line1, line2);
  lcd.setCursor(15, 0);
  lcd.write(0);  // Centang
}

/**
 * Indikator LED yang lebih efisien
 */
void blinkLED(int times = 1, int delayMs = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);  // ON (active low)
    delay(delayMs);
    digitalWrite(LED_PIN, HIGH);  // OFF
    if (i < times - 1) {
      delay(delayMs);
    }
  }
}

/**
 * Tampilkan mode operasi saat ini
 */
void showMode() {
  showMessage(F("Mode:"), getModeName(currentMode));
  blinkLED(1, 100);
}

/**
 * Tampilkan prompt untuk menunggu kartu
 */
void showCardPrompt() {
  showMessage(F("Siap Scan"), F("Tempel Kartu"));
}

/**
 * Inisialisasi LCD
 */
void setupLCD() {
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, checkChar);

  showMessage(F("SmartEduCard"), F("Memulai sistem..."));
  delay(DELAY_SHORT);
}

/**
 * Inisialisasi RFID Reader - optimasi timeout
 * @return true jika berhasil
 */
bool setupRFID() {
  showMessage(F("RFID Reader"), F("Mendeteksi..."));

  // Coba inisialisasi RFID reader
  nfc.begin();

  // Coba beberapa kali jika gagal terdeteksi di awal, dengan timeout lebih singkat
  uint32_t versionPN532;
  for (int attempt = 0; attempt < 2; attempt++) {
    versionPN532 = nfc.getFirmwareVersion();
    if (versionPN532) break;
    delay(300);  // Lebih singkat dari sebelumnya (500ms)
  }

  if (!versionPN532) {
    showMessage(F("Error RFID"), F("Reader tidak ada"));
    return false;
  }

  // Sukses terdeteksi, tampilkan versi firmware
  nfc.SAMConfig();

  char versionStr[10];
  sprintf(versionStr, "%d.%d", (versionPN532 >> 16) & 0xFF, (versionPN532 >> 8) & 0xFF);
  showSuccess(F("RFID Reader OK"), F("Versi: ") + String(versionStr));

  delay(DELAY_SHORT);
  return true;
}

/**
 * Login ke server dan dapatkan token JWT - optimasi dengan StaticJsonDocument
 * @return JWT token atau string kosong jika gagal
 */
String getToken() {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }

  showMessage(F("Login Server"), F("Mengautentikasi..."));

  HTTPClient http;

  // Siapkan request dengan timeout yang lebih efisien
  http.begin(wifiClient, getFlashString(LOGIN_URL));
  http.addHeader(F("Content-Type"), F("application/json"));
  http.setTimeout(7000);  // Lebih singkat (7 detik timeout)

  // Gunakan StaticJsonDocument untuk lebih efisien
  StaticJsonDocument<128> requestDoc;
  requestDoc["email"] = F("admin@gmail.com");
  requestDoc["password"] = F("admin123");

  String requestBody;
  serializeJson(requestDoc, requestBody);

  int httpCode = http.POST(requestBody);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    StaticJsonDocument<384> doc;  // Ukuran lebih kecil dan sesuai
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      showMessage(F("Error JSON"), error.c_str());
      http.end();
      return "";
    }

    // Pastikan token ada dalam respons
    if (!doc.containsKey("token")) {
      showMessage(F("Error Token"), F("Format invalid"));
      http.end();
      return "";
    }

    showSuccess(F("Login Berhasil"), F("Token diperoleh"));
    blinkLED(2, 150);
    delay(DELAY_SHORT);

    String token = doc["token"].as<String>();
    http.end();
    tokenTimestamp = millis();
    return token;
  } else {
    String errorMsg = (httpCode > 0) ? F("Kode: ") + String(httpCode) : F("Koneksi gagal");

    showMessage(F("Login Gagal"), errorMsg);
    delay(DELAY_SHORT);
    http.end();
    return "";
  }
}

/**
 * Periksa dan refresh token jika perlu
 * @return true jika token valid
 */
bool ensureValidToken() {
  // Token tidak ada atau sudah kadaluarsa
  if (jwtToken.length() == 0 || (millis() - tokenTimestamp > TOKEN_LIFETIME)) {
    showMessage(F("Token Expired"), F("Memperbarui..."));
    delay(DELAY_SHORT);

    String newToken = getToken();
    if (newToken.length() > 0) {
      jwtToken = newToken;
      tokenTimestamp = millis();
      return true;
    }
    return false;
  }
  return true;
}

/**
 * Baca UID kartu RFID dengan pengecekan error - optimasi efisiensi
 * @param uid Pointer ke string untuk menyimpan UID
 * @return true jika berhasil
 */
bool readCard(String* uid) {
  uint8_t uidBuffer[7];
  uint8_t uidLength;

  // Coba baca kartu dengan timeout yang lebih efisien
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuffer, &uidLength, 30)) {  // Timeout lebih singkat
    return false;
  }

  // Validasi panjang UID
  if (uidLength == 0 || uidLength > 7) {
    return false;
  }

  // Konversi UID ke string - optimasi dengan reserve memori
  uid->reserve(uidLength * 2);
  *uid = "";
  for (byte i = 0; i < uidLength; i++) {
    if (uidBuffer[i] < 0x10) {
      *uid += '0';
    }
    *uid += String(uidBuffer[i], HEX);
  }
  uid->toUpperCase();

  // Verifikasi UID tidak kosong
  if (uid->length() == 0) {
    return false;
  }

  // Debounce untuk kartu yang sama
  unsigned long now = millis();
  if (*uid == lastCardUID && (now - lastCardRead < CARD_SCAN_DELAY)) {
    return false;
  }

  // Update status
  lastCardUID = *uid;
  lastCardRead = now;

  showMessage(F("Kartu Terdeteksi"), uid->substring(0, 16));
  blinkLED(1, 100);
  delay(800);  // Delay tetap agar pengguna bisa membaca UID

  return true;
}

/**
 * Dapatkan URL API sesuai mode - optimasi dengan PROGMEM
 */
String getUrlForMode(OperationMode mode) {
  switch (mode) {
    case MODE_TRANS: return getFlashString(TRANS_URL);
    case MODE_GATE: return getFlashString(GATE_URL);
    case MODE_CLASS: return getFlashString(CLASS_URL);
    default: return getFlashString(LOG_URL);
  }
}

/**
 * Kirim data ke server dengan penanganan error - optimasi dengan StaticJsonDocument
 * @param uid UID kartu
 * @param url URL endpoint
 * @return Kode HTTP atau kode error kustom
 */
int sendToServer(const String& uid, const String& url) {
  if (WiFi.status() != WL_CONNECTED) {
    return RESP_WIFI_ERROR;
  }

  // Pastikan token valid
  if (!ensureValidToken()) {
    return RESP_TOKEN_ERROR;
  }

  HTTPClient http;

  // Siapkan request dengan JSON yang lebih efisien
  StaticJsonDocument<192> doc;  // Ukuran yang lebih tepat
  doc["uid"] = uid;
  doc["device_id"] = WiFi.macAddress();
  doc["mode"] = (int)currentMode;

  String payload;
  payload.reserve(128);  // Pre-alokasi memori
  if (serializeJson(doc, payload) == 0) {
    return RESP_JSON_ERROR;
  }

  http.begin(wifiClient, url);
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("Authorization"), F("Bearer ") + jwtToken);
  http.setTimeout(6000);  // 6 detik timeout (lebih singkat)

  int httpCode = http.POST(payload);

  // Handle 401 Unauthorized (token expired/invalid)
  if (httpCode == HTTP_CODE_UNAUTHORIZED) {
    showMessage(F("Token Invalid"), F("Memperbarui..."));

    // Coba refresh token
    jwtToken = getToken();

    // Jika refresh berhasil, coba request lagi
    if (jwtToken.length() > 0) {
      http.begin(wifiClient, url);
      http.addHeader(F("Content-Type"), F("application/json"));
      http.addHeader(F("Authorization"), F("Bearer ") + jwtToken);
      httpCode = http.POST(payload);
    } else {
      httpCode = RESP_TOKEN_ERROR;
    }
  }

  http.end();
  return httpCode;
}

/**
 * Proses respons dari server mode transaksi - optimasi dengan StaticJsonDocument
 */
void processTransactionResponse(const String& response) {
  StaticJsonDocument<512> doc;  
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    showMessage(F("Error Format"), F("Respons invalid"));
    return;
  }

  // Ekstrak message dari respons
  String message = doc["message"].as<String>();

  // Map pesan ke tampilan yang lebih baik
  if (message == "Check-in successful") {
    showSuccess(F("Jam Masuk"), F("Tercatat"));
  } else if (message == "Checkout successful") {
    showSuccess(F("Jam Pulang"), F("Tercatat"));
  } else if (message == "Checkout too early") {
    showMessage(F("Jam Pulang"), F("Terlalu cepat"));
  } else if (message == "Invalid card") {
    showMessage(F("Kartu"), F("Tidak Terdaftar"));
  } else if (message == "Already checked out") {
    showMessage(F("Jam Pulang"), F("Sudah Tercatat"));
  } else {
    // Tampilkan pesan asli jika tidak dikenali
    showMessage(message.substring(0, 16));
  }
}

/**
 * Handle respons server untuk semua mode
 */
void handleServerResponse(int httpCode, const String& uid) {
  // Handle error kode kustom
  if (httpCode == RESP_WIFI_ERROR) {
    showMessage(F("Error WiFi"), F("Tidak tersambung"));
    return;
  } else if (httpCode == RESP_TOKEN_ERROR) {
    showMessage(F("Error Token"), F("Autentikasi gagal"));
    return;
  } else if (httpCode == RESP_JSON_ERROR) {
    showMessage(F("Error Format"), F("Data invalid"));
    return;
  } else if (httpCode == RESP_TIMEOUT) {
    showMessage(F("Timeout"), F("Server lambat"));
    return;
  } else if (httpCode < 0) {
    showMessage(F("Error Koneksi"), F("Kode: ") + String(httpCode));
    return;
  } else if (httpCode != HTTP_CODE_OK) {
    showMessage(F("Error Server"), F("Kode: ") + String(httpCode));
    return;
  }

  // Untuk mode log, tampilkan sukses
  if (currentMode == MODE_LOG) {
    showSuccess(F("Log Aktivitas"), F("Berhasil"));
    blinkLED(2, 150);
    return;
  }

  // Untuk mode lain, kirim ke endpoint khusus
  String modeUrl = getUrlForMode(currentMode);

  HTTPClient http;

  // Persiapkan request kedua yang lebih efisien
  StaticJsonDocument<192> doc;  // Ukuran yang tepat
  doc["uid"] = uid;
  doc["device_id"] = WiFi.macAddress();

  String payload;
  payload.reserve(128);  // Pre-alokasi memori
  serializeJson(doc, payload);

  http.begin(wifiClient, modeUrl);
  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("Authorization"), F("Bearer ") + jwtToken);
  http.setTimeout(6000);  // 6 detik timeout (lebih singkat)

  showMessage(F("Proses ") + getModeName(currentMode), F("Mohon tunggu..."));

  int modeHttpCode = http.POST(payload);

  // Handle 401 Unauthorized di request kedua
  if (modeHttpCode == HTTP_CODE_UNAUTHORIZED && ensureValidToken()) {
    http.begin(wifiClient, modeUrl);
    http.addHeader(F("Content-Type"), F("application/json"));
    http.addHeader(F("Authorization"), F("Bearer ") + jwtToken);
    modeHttpCode = http.POST(payload);
  }

  if (modeHttpCode == HTTP_CODE_OK) {
    String response = http.getString();
    processTransactionResponse(response);
    blinkLED(2, 150);
  } else {
    String errorMsg = (modeHttpCode > 0) ? F("Kode: ") + String(modeHttpCode) : F("Koneksi gagal");

    showMessage(F("Error Transaksi"), errorMsg);
    blinkLED(3, 100);
  }

  http.end();
}

/**
 * Cek status WiFi dan reconnect jika perlu - optimasi timeout
 */
bool ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  showMessage(F("WiFi Terputus"), F("Menyambung ulang"));

  // Coba reconnect
  WiFi.reconnect();

  // Tunggu beberapa saat, dengan timeout yang lebih singkat
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 4000) {  
    delay(300);                                                           
  }

  if (WiFi.status() != WL_CONNECTED) {
    showMessage(F("WiFi Gagal"), F("Coba lagi..."));
    blinkLED(3, 100);
    return false;
  }

  showSuccess(F("WiFi Tersambung"), WiFi.SSID());
  delay(DELAY_SHORT);
  return true;
}

/**
 * Setup
 */
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== SmartEduCard System (Optimized) ==="));

  // Prealocate memory untuk string
  jwtToken.reserve(256);
  lastCardUID.reserve(16);

  // Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // OFF (active low)

  // Setup button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Inisialisasi SPI
  SPI.begin();

  // Inisialisasi LCD
  setupLCD();

  // Inisialisasi RFID - lanjutkan meskipun error
  if (!setupRFID()) {
    delay(DELAY_LONG);
  }

  // Setup WiFi
  showMessage(F("Menghubungkan"), F("WiFi..."));

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);  // 3 menit timeout untuk konfigurasi

  // Optimasi WiFi - atur mode WiFi untuk performa lebih baik
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // Hindari sleep mode yang menyebabkan koneksi lambat
  WiFi.setAutoReconnect(true);

  if (!wifiManager.autoConnect("Taiba_Smartcard", "password123")) {
    showMessage(F("Error WiFi"), F("Restart sistem"));
    delay(DELAY_LONG);
    ESP.restart();
  }

  showSuccess(F("WiFi Terhubung"), WiFi.SSID());
  delay(DELAY_SHORT);

  // Login dan dapatkan token
  jwtToken = getToken();
  if (jwtToken.length() == 0) {
    showMessage(F("Error Token"), F("Restart sistem"));
    delay(DELAY_LONG);
    ESP.restart();
  }

  // System ready
  systemReady = true;

  // Tampilkan mode awal
  showMode();
  delay(DELAY_SHORT);

  // Tampilkan prompt
  showCardPrompt();
}

/**
 * Loop - optimasi dengan yield()
 */
void loop() {
  // Cek tombol mode dengan debounce
  if (millis() - lastButtonPress > DEBOUNCE_DELAY) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      lastButtonPress = millis();
      currentMode = (OperationMode)((currentMode + 1) % MODE_COUNT);
      showMode();
      delay(DELAY_SHORT);
      showCardPrompt();
    }
  }

  // Pastikan WiFi terhubung - cek lebih jarang untuk meningkatkan performa
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 10000) {  // Cek tiap 10 detik saja
    lastWifiCheck = millis();
    if (!ensureWiFiConnected()) {
      delay(1000);
      return;
    }
  }

  // Baca kartu RFID
  String uid;
  if (readCard(&uid)) {
    // Kirim log aktivitas (selalu)
    showMessage(F("Mengirim Data"), F("Mohon tunggu..."));
    int httpLogCode = sendToServer(uid, getFlashString(LOG_URL));

    // Handle respons
    handleServerResponse(httpLogCode, uid);

    // Tunggu sejenak agar pesan bisa dibaca
    delay(DELAY_LONG);

    // Tampilkan prompt untuk scan berikutnya
    showCardPrompt();
  }

  // Memberikan kesempatan bagi sistem untuk menangani tugas lain
  yield();
}