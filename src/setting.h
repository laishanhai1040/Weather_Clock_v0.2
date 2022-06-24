#include <Arduino.h>

String reqUserKey = "888888";
String reqLocation = "99999";
static const char ntpServerName[] = "ntp.aliyun.com";
const int timeZone = 8;

const char* mqttServer = "iot.ranye-iot.net";
const char* mqttUserName = "hai1040_3";
const char* mqttPassWord = "245006";
const char* mqttClientId = "hai1040_3_id";

const int subQos = 1;
const bool cleanSession = false;

const char* willTopic = "willTopic";
const char* willMsg = "willMsg";
const int willQos = 0;
const int willRetain = false;

const char* PM10 = "hai1040/PMS5003T/pmat10";
const char* PM25 = "hai1040/PMS5003T/pmat25";
const char* PM100 = "hai1040/PMS5003T/pmat100";
const char* TEMPTopic = "hai1040/PMS5003T/temp";
const char* HUMITopic = "hai1040/PMS5003T/humi";
const char* BACKLIGHT = "hai1040/ILI9341/backlight";
const char* BGPCT = "hai1040/ILI9341/BGPChangeTime";
const char* BGPNUM = "hai1040/ILI9341/BGPnumber";


int bgChgInterval = 300;
int timeCheckInterval = 300;

const int START_BG_NUM = 1;
const int END_BG_NUM = 50;
const int MAX_INFO_ID = 10;