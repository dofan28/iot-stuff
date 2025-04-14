// Pin 8 untuk LED
const int pinLED = 8;
void setup() {
 // pin LED sebagai output
 pinMode(pinLED, OUTPUT);
}
// awal time delay 1000 | 1 detik
int timeDelay = 1000;
void loop() {

 // selama nilai timeDelay > 0
 // eksekusi blok program ini
 while(timeDelay > 0){
 // LED hidup mati dengan durasi 500 milisekon
 digitalWrite(pinLED, HIGH);
 delay(500);
 digitalWrite(pinLED, LOW);
 delay(500);

 // kurangi timeDelay dengan 100
 timeDelay = timeDelay - 100;
 }

 // setelah timeDelay dikurangi terus-menerus
 // maka pada akhirnya akan bernilai minus atau < 0
 // maka while di atas akan berhenti

 // selama nilai timeDelay < 1000
 // eksekusi blok program ini
 while(timeDelay < 1000){
 // LED hidup mati dengan durasi 100 milisekon
 digitalWrite(pinLED, HIGH);
 delay(100);
 digitalWrite(pinLED, LOW);
 delay(100);

 // tambahkan timeDelay dengan 100
 timeDelay = timeDelay + 100;
 }
}
