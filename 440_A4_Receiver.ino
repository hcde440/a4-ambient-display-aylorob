
#include <ESP8266WiFi.h>    
#include <PubSubClient.h>   
#include <ESP8266HTTPClient.h>                            
#include <ArduinoJson.h>                                  
#include <Adafruit_Sensor.h>                              
#include <DHT.h>
#include <DHT_U.h>
#include <Wire.h>    //Include these libraries
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>
#include <Servo.h>

const char* host = "trefle.io";
const int httpsPort = 443;

// Use web browser to view and copy
// SHA1 fingerprint of the certificate
const char* fingerprint = "F6 E9 41 84 9F A6 FA 0E 89 2A 3E 79 95 D5 C8 51 60 8F 5A BF";

#include "config.h"

//////////
//So to clarify, we are connecting to and MQTT server
//that has a login and password authentication
//I hope you remember the user and password
//////////

#define mqtt_server "mediatedspaces.net"  //this is its address, unique to the server
#define mqtt_user "hcdeiot"               //this is its server login, unique to the server
#define mqtt_password "esp8266"           //this is it server password, unique to the server

//////////
//We also need to publish and subscribe to topics, for this sketch are going
//to adopt a topic/subtopic addressing scheme: topic/subtopic
//////////

WiFiClient espClient;             //blah blah blah, espClient
PubSubClient mqtt(espClient);     //blah blah blah, tie PubSub (mqtt) client to WiFi client

//////////
//We need a 'truly' unique client ID for our esp8266, all client names on the server must be unique.
//Every device, app, other MQTT server, etc that connects to an MQTT server must have a unique client ID.
//This is the only way the server can keep every device separate and deal with them as individual devices/apps.
//The client ID is unique to the device.
//////////

char mac[6]; //A MAC address is a 'truly' unique ID for each device, lets use that as our 'truly' unique user ID!!!

//////////
//In our loop(), we are going to create a c-string that will be our message to the MQTT server, we will
//be generous and give ourselves 200 characters in our array, if we need more, just change this number
//////////

char message[201]; //201, as last character in the array is the NULL character, denoting the end of the array

float hum = 30;           //hold humidity
String rating = "";      //hold the rating from the API
Servo myservo;           //create instance of the servo
int pos = 0;             //For the position of the servo   

void setup() {
  
  Serial.begin(115200);                           //Does what it says
  
  // wait for serial monitor to open
  while(! Serial);
  
  Serial.print("This board is running: ");
  Serial.println(F(__FILE__));                    //These four lines give description of of file name and date 
  Serial.print("Complied: ");
  Serial.println(F(__DATE__ " " __TIME__));

  myservo.attach(12);                             //Servo pin
  pinMode(16,INPUT_PULLUP);                       //Example button
  myservo.write(90);

//Get wifi and mqtt set up
  setup_wifi();                                   
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(callback); //register the callback function
  
// Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  Serial.print("connecting to ");
  Serial.println(host);
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

//Check the fingerprint that gives you access
  if (client.verify(fingerprint, host)) {
    Serial.println("certificate matches");
  } else {
    Serial.println("certificate doesn't match");
  }

//Set up the API query
  String url = "/api/plants/150774?token=";
  url += plant_key;
  Serial.print("requesting URL: ");
  Serial.println(url);

//Do some fancy stuff with getting the webpage
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: BuildFailureDetectorESP8266\r\n" +
               "Connection: close\r\n\r\n");

//Did you get it?
  Serial.println("request sent");
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  
  String line = client.readStringUntil('\n');
  if (line.startsWith("{\"state\":\"success\"")) {
    Serial.println("esp8266/Arduino CI successfull!");
  } else {
    Serial.println("esp8266/Arduino CI has failed");
  }
  //Serial.println("reply was:");
  //Serial.println("==========");
  //Serial.println(line);
  //Serial.println("==========");
  //Serial.println("closing connection");

  DynamicJsonBuffer  jsonBuffer; //blah blah blah a DJB
  JsonObject& root = jsonBuffer.parseObject(line); //parse it!

  if (!root.success()) { //well?
    Serial.println("parseObject() failed, are you sure this message is JSON formatted.");
    return;
  }

  rating = root["main_species"]["growth"]["moisture_use"].as<String>();
  Serial.println(rating);
}

/////SETUP_WIFI/////
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");  //get the unique MAC address to use as MQTT client ID, a 'truly' unique ID.
  Serial.println(WiFi.macAddress());  //.macAddress returns a byte array 6 bytes representing the MAC address
}                                     //5C:CF:7F:F0:B0:C1 for example

/////CONNECT/RECONNECT/////Monitor the connection to MQTT server, if down, reconnect
void reconnect() {
  // Loop until we're reconnected
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqtt.connect(mac, mqtt_user, mqtt_password)) { //<<---using MAC as client ID, always unique!!!
      Serial.println("connected");
      mqtt.subscribe("weather/+"); //we are subscribing to 'theTopic' and all subtopics below that topic
    } else {                        //please change 'theTopic' to reflect your topic you are subscribing to
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  
  if (!mqtt.connected()) {                                      //Reconnect if lost connection
    reconnect();
  }

  mqtt.loop();                                                  //this keeps the mqtt connection 'active'

  //according to https://observant.zendesk.com/hc/en-us/articles/208067926-Monitoring-Soil-Moisture-for-Optimal-Crop-Growth, optimal is 10% to 50%, so ranges are based on that
  int minimum = 0;
  int maximum = 0;
  Serial.println(pos);
  
  
  if (rating == "Low") {
    minimum = 10;
    maximum = 23;    
  }

  else if (rating == "Medium") {
    minimum = 23;
    maximum = 37;    
  }
  
  else if (rating == "High") {
    minimum = 37;
    maximum = 50;    
  }

  if (digitalRead(16) == 0){
    digitalWrite(16,HIGH);
    hum = 0;
    Serial.println("This is a test");
  }

  if (hum < minimum) {
    if (pos > 60) {
      pos -= 1;
    }
    else {
      pos = 120;
    }          
    myservo.write(pos);               //tell servo to go to position
    //Serial.println("too little");
    delay(300);                       //let it get there
  }

  else if (hum > maximum) {
    if (pos < 120) {
      pos += 1;
    }
    else {
      pos = 60;
    }
    myservo.write(pos);              //tell servo to go to position
    //Serial.println("too much");
    delay(300);                       //let it get there
  }

  else if (minimum < hum < maximum) {
    pos = 90;
    myservo.write(pos);              //tell servo to go to position
    //Serial.println("good");
    delay(300);                       //let it get there
  }
  
}

void callback(char* topic, byte* payload, unsigned int length) {
  
  Serial.println();
  Serial.println("//////////////////");                 //To seperate the sends
  Serial.println();
  
  Serial.println();
  Serial.print("Message arrived [");
  Serial.print(topic); //'topic' refers to the incoming topic name, the 1st argument of the callback function
  Serial.println("] ");

  DynamicJsonBuffer  jsonBuffer; //blah blah blah a DJB
  JsonObject& root = jsonBuffer.parseObject(payload); //parse it!

  if (!root.success()) { //well?
    Serial.println("parseObject() failed, are you sure this message is JSON formatted.");
    return;
  }

  String humd = root["hum"].as<String>();
  hum = humd.toFloat();
  
  Serial.print("The humidity is: ");
  Serial.println(humd);

}
