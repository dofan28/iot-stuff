// Inisialisasi Jumlah LED
const int numLED = 3; // Menggunakan 3 LED karena pin 11 digunakan untuk tombol
const int pinLED[numLED] = { 8, 9, 10 }; // Pin LED
const int pinButton = 11; // Pin tombol push button

int currentMode = 0; // Mode efek LED saat ini
bool lastButtonState = HIGH; // Status tombol sebelumnya

void setup() {
  // Inisialisasi semua pin LED sebagai OUTPUT
  for (int i = 0; i < numLED; i++) {
    pinMode(pinLED[i], OUTPUT);
  }

  // Inisialisasi pin tombol sebagai INPUT
  pinMode(pinButton, INPUT_PULLUP);
}

void loop() {
  // Baca status tombol
  bool currentButtonState = digitalRead(pinButton);

  // Deteksi perubahan status tombol
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    currentMode = (currentMode + 1) % 4; // Ganti mode (0 - 3)
    delay(200); // Debouncing tombol
  }

  lastButtonState = currentButtonState;

  // Pilih mode berdasarkan nilai currentMode
  switch (currentMode) {
    case 0:
      // Mode 0: Semua LED mati
      for (int i = 0; i < numLED; i++) {
        digitalWrite(pinLED[i], LOW);
      }
      break;

    case 1:
      // Mode 1: LED menyala secara berurutan
      for (int i = 0; i < numLED; i++) {
        digitalWrite(pinLED[i], HIGH);
        delay(300);
        digitalWrite(pinLED[i], LOW);
      }
      break;

    case 2:
      // Mode 2: Semua LED menyala dan mati bersama
      for (int i = 0; i < numLED; i++) {
        digitalWrite(pinLED[i], HIGH);
      }
      delay(500);
      for (int i = 0; i < numLED; i++) {
        digitalWrite(pinLED[i], LOW);
      }
      delay(500);
      break;

    case 3:
      // Mode 3: LED berkedip secara acak
      for (int i = 0; i < 5; i++) {
        int randomLED = random(0, numLED);
        digitalWrite(pinLED[randomLED], HIGH);
        delay(200);
        digitalWrite(pinLED[randomLED], LOW);
      }
      break;
  }
}
