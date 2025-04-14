// Inisialisasi Jumlah LED
const int numLED = 4;
// LED 1,2,3,&4 jadi 1 varibel
// dengaan alamat index 0,1,2,3
const int pinLED[numLED] = {8,9,10,11};
void setup() {
 // Inisialisasi semua pin LED sebagai OUTPUT
 for(int i=0; i<4; i++){
 pinMode(pinLED[i], OUTPUT);
 }
}
void loop() {
 // Matikan semua LED
 for(int i=0; i<4; i++){
 digitalWrite(pinLED[i], LOW);
 }
 delay(1000);

 // Hidupkan semua LED bertahap dg jeda 1 detik
 for(int i=0; i<4; i++){
 digitalWrite(pinLED[i], HIGH);
 delay(1000);
 }
}