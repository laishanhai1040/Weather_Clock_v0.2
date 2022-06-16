#include <Arduino.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
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
ESP8266WebServer esp8266_server(80);

File fsUploadFile;

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

  Serial.println('\n');
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());              // 通过串口监视器输出连接的WiFi名称
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
  
  ntpInit();
  
  displayBg(bgId);
  tft.fillRect(0, 0, BANNER_WIDTH, BANNER_HEIGHT, BANNER_BG);
  tft.fillRect(0, 220, BANNER_WIDTH, BANNER_HEIGHT, BANNER_BG);

  mqttClient.setServer(mqttServer, 1883);
  mqttClient.setCallback(receiveCallback);
  connectMQTTserver();

  esp8266_server.on("/upload.html", HTTP_POST, respondOK, handleFileUpload);

  esp8266_server.onNotFound(handleUserRequest);

  esp8266_server.begin();
  Serial.println("HTTP Server started");

  //ticker.attach(1, tickerCount);
}

void loop() {
  esp8266_server.handleClient();
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

// 处理上传文件函数
void handleFileUpload(){  
  
  HTTPUpload& upload = esp8266_server.upload();
  
  if(upload.status == UPLOAD_FILE_START){                     // 如果上传状态为UPLOAD_FILE_START
    
    String filename = upload.filename;                        // 建立字符串变量用于存放上传文件名
    if(!filename.startsWith("/")) filename = "/" + filename;  // 为上传文件名前加上"/"
    Serial.println("File Name: " + filename);                 // 通过串口监视器输出上传文件的名称

    fsUploadFile = SPIFFS.open(filename, "w");            // 在SPIFFS中建立文件用于写入用户上传的文件数据
    
  } else if(upload.status == UPLOAD_FILE_WRITE){          // 如果上传状态为UPLOAD_FILE_WRITE      
    
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // 向SPIFFS文件写入浏览器发来的文件数据
      
  } else if(upload.status == UPLOAD_FILE_END){            // 如果上传状态为UPLOAD_FILE_END 
    if(fsUploadFile) {                                    // 如果文件成功建立
      fsUploadFile.close();                               // 将文件关闭
      Serial.println(" Size: "+ upload.totalSize);        // 通过串口监视器输出文件大小
      esp8266_server.sendHeader("Location","/success.html");  // 将浏览器跳转到/success.html（成功上传页面）
      esp8266_server.send(303);                               // 发送相应代码303（重定向到新页面） 
      fex.listSPIFFS();
    } else {                                              // 如果文件未能成功建立
      Serial.println("File upload failed");               // 通过串口监视器输出报错信息
      esp8266_server.send(500, "text/plain", "500: couldn't create file"); // 向浏览器发送相应代码500（服务器错误）
    }    
  }
}

//回复状态码 200 给客户端
void respondOK(){
  esp8266_server.send(200);
}

// 处理用户浏览器的HTTP访问
void handleUserRequest(){
                              
  // 获取用户请求网址信息
  String webAddress = esp8266_server.uri();
  
  // 通过handleFileRead函数处处理用户访问
  bool fileReadOK = handleFileRead(webAddress);

  // 如果在SPIFFS无法找到用户访问的资源，则回复404 (Not Found)
  if (!fileReadOK){                                                 
    esp8266_server.send(404, "text/plain", "404 Not Found"); 
  }
}

bool handleFileRead(String path) {            //处理浏览器HTTP访问

  if (path.endsWith("/")) {                   // 如果访问地址以"/"为结尾
    path = "/index.html";                     // 则将访问地址修改为/index.html便于SPIFFS访问
  } 
  
  String contentType = getContentType(path);  // 获取文件类型
  
  if (SPIFFS.exists(path)) {                     // 如果访问的文件可以在SPIFFS中找到
    File file = SPIFFS.open(path, "r");          // 则尝试打开该文件
    esp8266_server.streamFile(file, contentType);// 并且将该文件返回给浏览器
    file.close();                                // 并且关闭文件
    return true;                                 // 返回true
  }
  return false;                                  // 如果文件未找到，则返回false
}

// 获取文件类型
String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
