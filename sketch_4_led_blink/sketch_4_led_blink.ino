// Inisialisasi Pin LED
const int pinLED1 = 8;
const int pinLED2 = 9;
const int pinLED3 = 10;
const int pinLED4 = 11;

void setup() {
  // pin LED sebagai output
  pinMode(pinLED1, OUTPUT);
  pinMode(pinLED2, OUTPUT);
  pinMode(pinLED3, OUTPUT);
  pinMode(pinLED4, OUTPUT);
}

void loop() {
  // Pola kompleks dengan berbagai efek

  // Pola 1: LED bergantian secara zig-zag
  for (int i = 0; i < 5; i++) {
    digitalWrite(pinLED1, HIGH);
    digitalWrite(pinLED4, HIGH);
    delay(300);
    digitalWrite(pinLED1, LOW);
    digitalWrite(pinLED4, LOW);
    digitalWrite(pinLED2, HIGH);
    digitalWrite(pinLED3, HIGH);
    delay(300);
    digitalWrite(pinLED2, LOW);
    digitalWrite(pinLED3, LOW);
  }

  // Jeda 1 detik
  delay(1000);

  // Pola 2: Efek "breathing" pada semua LED
  for (int brightness = 0; brightness <= 255; brightness += 5) {
    analogWrite(pinLED1, brightness);
    analogWrite(pinLED2, brightness);
    analogWrite(pinLED3, brightness);
    analogWrite(pinLED4, brightness);
    delay(30);
  }
  for (int brightness = 255; brightness >= 0; brightness -= 5) {
    analogWrite(pinLED1, brightness);
    analogWrite(pinLED2, brightness);
    analogWrite(pinLED3, brightness);
    analogWrite(pinLED4, brightness);
    delay(30);
  }

  // Jeda 1 detik
  delay(1000);

  // Pola 3: LED menyala satu per satu maju dan mundur
  for (int i = pinLED1; i <= pinLED4; i++) {
    digitalWrite(i, HIGH);
    delay(200);
    digitalWrite(i, LOW);
  }
  for (int i = pinLED4; i >= pinLED1; i--) {
    digitalWrite(i, HIGH);
    delay(200);
    digitalWrite(i, LOW);
  }

  // Jeda 1 detik
  delay(1000);

  // Pola 4: Efek "random" LED menyala
  for (int i = 0; i < 10; i++) {
    int randomLED = random(pinLED1, pinLED4 + 1);
    digitalWrite(randomLED, HIGH);
    delay(150);
    digitalWrite(randomLED, LOW);
  }

  // Jeda 2 detik sebelum mengulang pola
  delay(2000);
}
