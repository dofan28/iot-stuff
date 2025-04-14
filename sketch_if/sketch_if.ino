// Pin 8 untuk LED
const int pinLED = 8;
void setup() {
 // pin LED sebagai output
 pinMode(pinLED, OUTPUT);
}
// awal time delay 1000 | 1 detik
int timeDelay = 1000;
void loop() {
 // Setiap looping, nilai timeDelay dikurangi 100
 timeDelay = timeDelay - 100;

  /* Jika timeDelay bernilai 0 atau negatif
 maka nilai timeDelay direset ke 1000
 */
 if(timeDelay <= 0){
 timeDelay = 1000;
 }

 //Nyalakan dan matikan LED selama timeDelay
 digitalWrite(pinLED, HIGH);
 delay(timeDelay);
 digitalWrite(pinLED, LOW);
 delay(timeDelay);
}
