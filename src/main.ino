#include <Arduino.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define FS_NO_GLOBALS
#include <FS.h>
#include <TFT_eSPI.h>
#include <TFT_eFEX.h>
//#include <Ticker.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

#include "setting.h"
//#include "chinese.h"

#define BANNER_HEIGHT 20
#define BANNER_WIDTH 320
#define HALF_BANNER_WIDTH 160
#define BANNER_BG TFT_BLACK
#define BANNER_FG TFT_WHITE

TFT_eSPI tft = TFT_eSPI();
TFT_eFEX fex = TFT_eFEX(&tft);
bool bgChangeFlag = false;
bool BLchangeFlage = false;
bool BGPCTFlage = false;
int bgId = START_BG_NUM;
int BackLightValue = 128;

WiFiUDP Udp;
unsigned int localPort = 8888;
time_t prevDisplay = 0;

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

//Ticker ticker;
int bgCounter;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


String topicString;
byte receiveVar;
byte printVar;

byte receivePM10, receivePM25, receivePM100;
int receiveTEMP, receiveHUMI, receiveBackLight;


void setup() {
  Serial.begin(9600);

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield();
  }
  Serial.println("\r\nSPIFFS initialised.");

  pinMode(D2, OUTPUT);
  analogWrite(D2, 50);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  WiFiManager wifiManager;
  wifiManager.autoConnect("AutoConnectAP");
  Serial.print("WiFi Connected!");
  tft.setTextColor(TFT_BLUE);
  tft.drawString("WiFi Connected!", 0,30,4);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("WiFi SSID:", 0,60,4);
  tft.drawString(WiFi.SSID(),0,90,4);
  
  ntpInit();
  
  displayBg(bgId);
  tft.fillRect(0, 0, BANNER_WIDTH, BANNER_HEIGHT, BANNER_BG);
  tft.fillRect(0, 220, BANNER_WIDTH, BANNER_HEIGHT, BANNER_BG);

  mqttClient.setServer(mqttServer, 1883);
  mqttClient.setCallback(receiveCallback);
  connectMQTTserver();

  //ticker.attach(1, tickerCount);
}

void loop() {
  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) {
      prevDisplay = now();
      tick();
    }
  }
  if (bgChangeFlag == true) {
    bgChangeFlag = false;
    bgId++;
    if (bgId > END_BG_NUM) bgId = START_BG_NUM;
    displayBg(bgId);
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
  } else {
    connectMQTTserver();
  }
  if (BLchangeFlage == true) {
    BackLightValue = map(receiveBackLight, 0,10, 0,255);
    Serial.print("BackLightValue=" );
    Serial.println(BackLightValue);
    BLchangeFlage = false;
  }
  analogWrite(D2, BackLightValue);
  mqttClient.loop();
}

void ntpInit() {
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local UDP port: ");
  Serial.println(Udp.localPort());
  Serial.println("---------------");
  setSyncProvider(getNtpTime);
  setSyncInterval(timeCheckInterval);
}

time_t getNtpTime() {
  IPAddress ntpServerIP;

  while (Udp.parsePacket() > 0);
  Serial.println("Transmit NTP Request");
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      // convert four byte starting at location 40 to long integer
      secsSince1900  = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR; 
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the givern address
void sendNTPpacket(IPAddress &address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;            // Stratum, or type of clock
  packetBuffer[2] = 6;            // Polling Interval
  packetBuffer[3] = 0xEC;         // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123);  // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}


// --- 定时调用计时函数---
void tick() {
  displayTime();      // 每秒更新时钟显示
  
  bgCounter++;

  if (bgCounter > bgChgInterval) {
    bgCounter = 0;
    bgChangeFlag = true;
  }
}

// ---时间显示---
void displayTime() {
  String timeString;
  String dateString;
  String dateTimeString;

  timeString = adjDigit(hour()) + ":" + adjDigit(minute()) + ":" + adjDigit(second());
  dateString = adjDigit(month()) + "-" + adjDigit(day());
  dateTimeString = dateString + "      " + timeString;

  tft.setViewport(0,0, BANNER_WIDTH, BANNER_HEIGHT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(BANNER_FG, BANNER_BG);
  tft.setTextPadding(10);
  tft.drawString(dateTimeString, 0, 0, 4);

  if (weekday() == 1) {
    tft.fillRect(300,0,20,20, BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Sun", 320,0,4);
  } else if (weekday() == 2){
    tft.fillRect(300,0,20,20,BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Mon", 320,0,4);
  } else if (weekday() == 3){
    tft.fillRect(300,0,20,20,BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Tue", 320,0,4);
  } else if (weekday() == 4){
    tft.fillRect(300,0,20,20,BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Wed", 320,0,4);
  } else if (weekday() == 5){
    tft.fillRect(300,0,20,20,BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Thu", 320,0,4);
  } else if (weekday() == 6){
    tft.fillRect(300,0,20,20,BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Fri", 320,0,4);
  } else if (weekday() == 7){
    tft.fillRect(300,0,20,20,BANNER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Sat", 320,0,4);
  } 

  tft.resetViewport();
}

String adjDigit(int number) {
  if(number >= 10) {
    return String(number);
  } else {
    return String("0" + String(number));
  }
}

void displayBg(int bgId) {
  String bgName = "/bg" + String(bgId) + ".jpg";
  fex.drawJpeg(bgName, 0, BANNER_HEIGHT);
  Serial.println(bgName);
}

void receiveCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (u_int i=0; i<length; i++) {
    Serial.print((char)payload[i]);
    receiveVar = receiveVar + (char)payload[i];
  }
  Serial.println("");
  Serial.print("Message Length(Bytes) ");
  Serial.println(length);

  payload[length] = '\0';
  String s = String((char*)payload);
  int ii = s.toInt();
  Serial.print("ii=");
  Serial.println(ii);

  String topic2 = topic;
  if (topic2 == PM10) {
    receivePM10 = ii;
  }
  else if (topic2 == PM25) {
    receivePM25 = ii;
  }
  else if (topic2 == PM100) {
    receivePM100 = ii;
  }
  else if (topic2 == TEMPTopic) {
    receiveTEMP = ii;
  }
  else if (topic2 == HUMITopic) {
    receiveHUMI = ii;
  }
  else if (topic2 == BACKLIGHT) {
    receiveBackLight = ii;
    BLchangeFlage = true;
  }
  else if (topic2 == BGPCT) {
    bgChgInterval = ii;
    Serial.print("BackGround Picture Change Interval (s): ");
    Serial.println(bgChgInterval);
    BGPCTFlage = true;
  }
  else if (topic2 == BGPNUM) {
    bgId = ii;
    bgChangeFlag = true;
  }
}

void connectMQTTserver() {
  String clientId = mqttClientId;

  if (mqttClient.connect(clientId.c_str(), mqttUserName, mqttPassWord,
                          willTopic, willQos, willRetain, willMsg, cleanSession)) {
    Serial.println("MQTT Server Connected.");
    Serial.println("Server Address: ");
    Serial.println(mqttServer);
    Serial.println("ClientId: ");
    Serial.println(clientId);
    subscribeTopic();
  } else {
    Serial.print("MQTT Server Connect Failed. Client State: ");
    Serial.println(mqttClient.state());
    delay(5000);
  }
}

void subscribeTopic() {
  for(u_int i=0; i<8; i++) {
    switch (i) {
      case 0: 
        topicString = PM10;
        break;
      case 1: 
        topicString = PM25;
        break;
      case 2: 
        topicString = PM100;
        break;
      case 3: 
        topicString = TEMPTopic;
        break;
      case 4: 
        topicString = HUMITopic;
        break;
      case 5: 
        topicString = BACKLIGHT;
        break;
      case 6: 
        topicString = BGPCT;
        break;
      case 7: 
        topicString = BGPNUM;
        break;
      default:
        topicString = PM10;
    }
    char subTopic[topicString.length()+1];
    strcpy(subTopic, topicString.c_str());

    if(mqttClient.subscribe(subTopic, subQos)) {
      Serial.println("Subscrib Topic: ");
      Serial.println(subTopic);
    } else {
      Serial.print("Subscribe Fail...");
    }
  }
}
