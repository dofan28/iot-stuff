const int pinButton = 2;
const int pinLED = 8;

void setup() {
  pinMode(pinButton, INPUT);
  pinMode(pinLED, OUTPUT);

  // aktifkan pull-up resistor
  digitalWrite(pinButton, HIGH);
}
void loop() {
  if (digitalRead(pinButton) == LOW) {
    digitalWrite(pinLED, HIGH);
  } else {
    digitalWrite(pinLED, LOW);
  }
}