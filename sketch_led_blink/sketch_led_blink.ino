// Define LED pin
const int ledPin = 8;

void setup() {
  // Initialize the LED pin as an output
  pinMode(ledPin, OUTPUT);
}

void loop() {
  // Blink pattern: Fast blinking 3 times
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(200); // 200ms ON
    digitalWrite(ledPin, LOW);
    delay(200); // 200ms OFF
  }

  // Pause before the next pattern
  delay(1000); // 1 second pause

  // Blink pattern: Slow blinking 2 times
  for (int i = 0; i < 2; i++) {
    digitalWrite(ledPin, HIGH);
    delay(1000); // 1 second ON
    digitalWrite(ledPin, LOW);
    delay(1000); // 1 second OFF
  }

  // Pause before the next pattern
  delay(500); // 500ms pause

  // Blink pattern: SOS Morse Code (... --- ...)
  // S: 3 short blinks
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(200); // 200ms ON
    digitalWrite(ledPin, LOW);
    delay(200); // 200ms OFF
  }
  // O: 3 long blinks
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(600); // 600ms ON
    digitalWrite(ledPin, LOW);
    delay(200); // 200ms OFF
  }
  // S: 3 short blinks again
  for (int i = 0; i < 3; i++) {
    digitalWrite(ledPin, HIGH);
    delay(200); // 200ms ON
    digitalWrite(ledPin, LOW);
    delay(200); // 200ms OFF
  }

  // Pause before repeating the entire sequence
  delay(2000); // 2 seconds pause
}
