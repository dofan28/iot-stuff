// Pin 8 untuk LED
const int pinLED = 8;
void setup() {
 // pin LED sebagai output
 pinMode(pinLED, OUTPUT);
}
// awal time delay 1000 | 1 detik
int timeDelay = 3000;

void loop() {
 // perulangan sebanyak 10 kali dari 1 hingga 10
 for(int i=1; i<=10; i++){
 // LED hidup mati dengan durasi 500 milisekon
 digitalWrite(pinLED, HIGH);
 delay(500);
 digitalWrite(pinLED, LOW);
 delay(500);
 }
 // diam selama 3 detik
 delay(timeDelay);
}
