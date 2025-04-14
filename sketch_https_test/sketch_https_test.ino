#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>  // Tambahkan ini

const char* url = "https://jsonplaceholder.typicode.com/posts";
const char* ssid = "hotspot-banh";
const char* password = "dofan123";

WiFiClientSecure client;  // Buat object WiFiClientSecure

void setup() {
  Serial.begin(115200);
  delay(4000);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network");
  
  client.setInsecure();  // Bypass SSL certificate verification
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient https;
    
    // Gunakan client untuk koneksi HTTPS
    if(https.begin(client, url)) {  // Tambahkan client sebagai parameter
      int httpResponseCode = https.GET();
      
      if (httpResponseCode > 0) {
        String payload = https.getString();
        Serial.println(httpResponseCode);
        Serial.println(payload);
      } else {
        Serial.print("Error on sending GET: ");
        Serial.println(httpResponseCode);
      }
      
      https.end();
    } else {
      Serial.println("Error in HTTPS connection");
    }
  } else {
    Serial.println("Error in WiFi connection");
  }
  
  delay(10000);
}