
/*
  Web client

 This sketch connects to a website (http://www.google.com)
 using a WiFi shield.

 This example is written for a network using WPA encryption. For
 WEP or WPA, change the Wifi.begin() call accordingly.

 This example is written for a network using WPA encryption. For
 WEP or WPA, change the Wifi.begin() call accordingly.

 Circuit:
 * WiFi shield attached

 created 13 July 2010
 by dlf (Metodo2 srl)
 modified 31 May 2012
 by Tom Igoe
 */


#include <HTTPClient.h>
#include <WiFi.h>

//char ssid[] = "Tech_D0048070"; //  your network SSID (name)
//char pass[] = "UREZYUND";    // your network password (use for WPA, or use as key for WEP)
 
const char* ssid = "Tech_D0048070";
const char* password =  "UREZYUND";

char receivedChar;
 
void setup() {

  Serial.begin(115200);
  delay(4000);   //Delay needed before calling the WiFi.begin

}
 
void loop() {

  receivedChar = Serial.read();
  if (receivedChar == 'Y') {
    Serial.println("Got request to connect..");
   
    if (WiFi.status()== WL_CONNECTED) {   //Check WiFi connection status
   
      HTTPClient http;   
   
      http.begin("http://buratino.asobolev.ru/api/v1/devices/2e52e67d-d0f5-4f87-b7b6-9aae97a42623/readouts");
      //http.begin("http://jsonplaceholder.typicode.com/comments?id=10");
      //http.begin("http://example.com/index.html");
      http.addHeader("Content-Type", "application/json");             //Specify content-type header
  
      String postData = "{\"device\": \"2e52e67d-d0f5-4f87-b7b6-9aae97a42623\", \"timestamp\": \"2017-11-09T21:15:14.716959Z\", \"sensor_type\": \"TMP\", \"value\": 34.000662}";
   
      int httpResponseCode = http.POST(postData);   //Send the actual POST request
      //int httpResponseCode = http.GET(); 
   
      if (httpResponseCode>0) {
   
        String response = http.getString();                       //Get the response to the request
   
        Serial.println(httpResponseCode);   //Print return code
        Serial.println(response);           //Print request answer
   
      } else {
   
        Serial.print("Error on sending POST: ");
        Serial.println(httpResponseCode);
   
      }
   
      http.end();  //Free resources
   
    } else {
   
      Serial.println("Error in WiFi connection");
      connectToWiFi();
   
    }
  } else {
    delay(500);  //Send a request every 10 seconds
  }
  
}


void connectToWiFi() {
  WiFi.begin(ssid, password); 
 
  while (WiFi.status() != WL_CONNECTED) { //Check for the connection
    delay(5000);
    Serial.println("Connecting to WiFi..");
  }
 
  Serial.println("Connected to the WiFi network");
}


