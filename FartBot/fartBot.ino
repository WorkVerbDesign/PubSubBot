#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoOTA.h>
#include "config.h" 

/* arduino ESP twitch pubsub keyword keyword
 *  
 *  This code is intended originally for a fartbot but here we are.
 *  Twitch pubsub documentation: https://dev.twitch.tv/docs/pubsub
 *  
 *  ArduinoJson v6 (https://arduinojson.org/)
 *  ESP8266Wifi and ArduinoOTA 2.6.3 (URL for board manager)(http://arduino.esp8266.com/stable/package_esp8266com_index.json)
 *  Arduino Websockets by gilmaimon 0.4.15 (https://github.com/gilmaimon/ArduinoWebsockets)
 *  
 *  
 *  The code sets up all the libraries and then uses a state machine
 *  to handle the login/relogin procedure. 
 *  The final two states are dictated by a ping timer.
 *  Most of the stuff in here is untested so if the software kills your cat
 *  don't blame me.
 *  
 *  Also rename the config file and put your stuff innit
 */


//pins
#define PING_TIME 240000 //+- 10s using random later
#define REAPING 12000 //ms for no ping reconnect
#define RECON_TIME 1000 //initial time to reconnect
#define RECON_UPTO 120000 //max reconnect time
#define RESPONSETIME 5000 //time to wait for login response from server
#define BUTTONDELAY 200 //ms for button press
#define TOOTTIME 3500 //ms delay before next button
#define DEBUG 0 //serial print or not

//webSettings
const char* robotName = "FartBox2000";
const char* ssid = "GloriousPCMR";
const char* password = "deadbeef";

String nonce = "";
const String websockets_connection_string = "wss://pubsub-edge.twitch.tv"; //Enter server adress
const String echo_org_ssl_fingerprint = "f6 44 ca 49 a6 ff 6c 0e 4c e6 f5 a9 05 dc 7b fb 76 9c 37 0d"; //pubsub edge twitch tv fingerprint (WILDCARD!)

//fartbox settings
const uint8_t farts[9] = {16, 14, 12, 13, 2, 4, 5, 3, 1};
const uint8_t fartSize = 9;

//variables
int stater = 0;
int reconMult = 0; //multiplier for reconnection attemps
unsigned int queueue = 0; //farts in the pipe

//timing vars
unsigned long pptimer = 0; //time for next ping
unsigned long timerpp = 0; //time since sent ping
unsigned long clientLast = 0; //last time we tried
unsigned long loginAt = 0; //time login was sent
unsigned long lastToot = 0; //when'd you last fart bro


//socket stuff
using namespace websockets;
WebsocketsClient client;

//json stuff
const size_t capacity = 2*JSON_OBJECT_SIZE(2) + 1866;
DynamicJsonDocument doc(capacity);

//=============SETUP================
void setup() {
  setupWifi();
  setupOTA();
  setupPins();
  setupSerial();
  setupSocketStuff();
  setupDONE();
}

//============LOOP==============
void loop() {
  checkWifi();
  ArduinoOTA.handle();
  loginMachine();
  fartMachine();
}

//=========setup Stuff========
void setupOTA(){
  ArduinoOTA.setHostname(robotName);
  ArduinoOTA.begin();
}

void setupPins(){
  //builtin LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  for(uint8_t i = 0; i < fartSize; i++){
    pinMode(farts[i], OUTPUT); 
    digitalWrite(farts[i], LOW);
  }
}

void setupSocketStuff(){
  client.onMessage(onMessageCallback);
  client.onEvent(onEventsCallback);
  client.setFingerprint(echo_org_ssl_fingerprint.c_str());
}

void setupWifi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if(WiFi.waitForConnectResult() != WL_CONNECTED){
    delay(5000);
    ESP.restart();
  }
}

void setupSerial(){
  if(DEBUG){
    Serial.begin(115200);
    Serial.println("starteding up");
  }
}

void setupDONE(){
  for(int i = 5; i > 0; i--){
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(10);
  }
}
//==========loop functions========
void checkWifi(){
  //esp automagically reconnects so wait if disconnected
  if(WiFi.status() != WL_CONNECTED){
    delay(5000);
    ESP.restart();
  }
}

//========= state machine ========
//uses stater to login to twitch api ESP handles wifi
void loginMachine(){
  switch(stater){    
    case 0:  //login to twitch pubsub
      connectClient();
      break;
    case 1:  //subscribe to the topic
      login();
      break;
    case 2:  //wait for response
      client.poll();
      responseTimer();
      break;
    case 3:   //normal running mode
      client.poll();
      pponging();
      break;
    case 4:
      client.poll();
      waitForPong();
      break;
  }
}

//============state machine functions=============
void connectClient(){  
  if(millis() - clientLast > constrain(RECON_TIME*reconMult, 0, RECON_UPTO)){
    reconMult++;
    client.connect(websockets_connection_string);
    clientLast = millis();
  }
}

void login(){
  //make nonce
  nonce = "e4t5v345nz3sm";
  //in case you friggen tryhards want random nonces
  //nonce = string(RANDOM_REG32);
  
  const String logmein = "{\"type\": \"LISTEN\",\"nonce\": \""+nonce+"\",\"data\": {\"topics\": [\"channel-points-channel-v1."+chanId+"\"],\"auth_token\": \""+OAuth+"\"}}";
  client.send(logmein);
  loginAt = millis();
  stater = 2;
}
void responseTimer(){
  if(millis() - loginAt > RESPONSETIME){
    //too much time passed, try again
    resetEverything();
  }
}

//twitch requires a ping every 4 mins +- 10s, but it's its own format
void pponging(){
  if((millis() - pptimer) > PING_TIME){
    //send ping
    client.send("{\"type\": \"PING\"}");
    
    //fail timer and set timer so it doesn't trigger multiple times
    timerpp = pptimer = millis();

    //state up
    stater = 4;
  }
}

void waitForPong(){
  if((millis() - timerpp) > REAPING){
    //you fail
    resetEverything();
  }
}

//============ FART MACHINE==========
void fartMachine(){
  bool SBD = millis() - lastToot > TOOTTIME;
  if(queueue > 0 && SBD){
    //choose random fart
    uint8_t whoFarted = random(fartSize);
    
    //trigger the pin
    digitalWrite(farts[whoFarted], HIGH);
    delay(BUTTONDELAY);
    digitalWrite(farts[whoFarted], LOW);

    //book keeping
    lastToot = millis();
    queueue--;
  }
}

//============ Callbacks =============
//do we even get this or is it a message?
void onEventsCallback(WebsocketsEvent event, String data) {
    if(event == WebsocketsEvent::ConnectionOpened){
      if(DEBUG){
        Serial.println("Connnection Opened");
      }
      reconMult = 0;
      stater = 1; 
        
    }else if(event == WebsocketsEvent::ConnectionClosed){
      if(DEBUG){
        Serial.println("Connnection Closed");
      }
      resetEverything();   
    }
}

void onMessageCallback(WebsocketsMessage message) {
  //print it
  if(DEBUG){
    Serial.print("Got Message: ");
    Serial.println(message.data());
  }

  //parse main message, get message type
  deserializeJson(doc, message.data());  
  const String initialType = doc["type"];

  if(DEBUG){
    Serial.println(initialType);
  }
  
  if(initialType == "RECONNECT"){resetEverything();}
  if(initialType == "PONG"){resetPing();}
  if(initialType == "RESPONSE"){responseChecks();}
  if(initialType == "MESSAGE"){parseMessage();}
}

void resetEverything(){
  client.close();
  stater = 0;
}

void resetPing(){
  pptimer = millis() - random(0,20000);
  stater = 3;
}

void responseChecks(){
  if(doc["nonce"] != nonce){
    if(DEBUG){
      Serial.println("NONCE FAIL");
    }
    resetEverything();
  }
  if(doc["error"] != ""){
    if(DEBUG){
      Serial.println("error message");
    }
    resetEverything();
  }
  if(DEBUG){
    Serial.println("RESPONSEDED");
  }
  resetPing();
}

void parseMessage(){
  queueue++;
  if(DEBUG){
    Serial.print("queue: ");
    Serial.println(queueue);
  }
}
