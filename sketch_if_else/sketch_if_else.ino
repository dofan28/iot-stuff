// Pin 8 untuk LED
const int pinLED = 8;

// Awal nilai time delay (1000 ms = 1 detik)
int timeDelay = 1000;

void setup() {
  // Atur pin LED sebagai output
  pinMode(pinLED, OUTPUT);
}

void loop() {
  /* Jika nilai timeDelay lebih kecil atau sama dengan 100,
     LED akan diam (tidak berkedip) selama 3 detik.
     Setelah itu, nilai timeDelay direset ke 1000.
  */
  if (timeDelay <= 100) {
    delay(3000); // Tunggu selama 3 detik
    timeDelay = 1000; // Reset timeDelay ke 1000 ms
  } else {
    // Kurangi nilai timeDelay sebesar 100 jika masih > 100
    timeDelay -= 100;
  }

  // Nyalakan LED selama timeDelay
  digitalWrite(pinLED, HIGH);
  delay(timeDelay);

  // Matikan LED selama timeDelay
  digitalWrite(pinLED, LOW);
  delay(timeDelay);
}
