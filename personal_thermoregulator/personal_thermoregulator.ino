#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <movingAvg.h>

/*Put your SSID & Password*/
const char* ssid = "Thermoregulator";  // Enter SSID here
const char* password = "donarudo";  //Enter Password here

const byte DNS_PORT = 53;
IPAddress apIP(172, 217, 28, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

//pin setup
int ledPin = 4; //d2
int peltierPin = 12; //d6
int batteryPin = A0;

//peltier setup
int peltierOnTimeModes[] = {3500,4000,4500};
int peltierOnTime = 0;
int peltierOffTimeModes[] = {17000,16000,15000};
int peltierOffTime = 0;

int peltierPwmMax = 255;
int peltierPwmMin = 25;
int peltierState = 0; //0:off 1:on
int peltierTargetPwm = 0;
int peltierCurrentPwm = 0;
unsigned long peltierTimer = 0;

int coolingMode = 0;

boolean ledEnabled = true;

//battery
float voltageReadingMultip = 0.2212;
int voltageReadingMax = 4050 * voltageReadingMultip; //4.05V
int voltageReadingMin = 3450 * voltageReadingMultip; //3.45V
int batteryLevel = 100;
movingAvg batteryReadingAvg(80);
int batterySamplingTime = 1000; //ms
unsigned long batteryMeasureTimer = 0;



void setup() {
  Serial.begin(115200);
  batteryReadingAvg.begin();
  delay(100);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(peltierPin, OUTPUT);
  digitalWrite(peltierPin, LOW);
  updateCoolingMode(1); //set default mode here

  Serial.println("Launching PTR Captive Portal");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.start(DNS_PORT, "*", apIP);

  webServer.on("/0", handleCoolingMode0);
  webServer.on("/1", handleCoolingMode1);
  webServer.on("/2", handleCoolingMode2);
  webServer.on("/disable_led", handleDisableLed);
  webServer.on("/disable_wifi", handleDisableWifi);
  webServer.onNotFound(showControlHtml);
  delay(100);

  webServer.begin();
  Serial.println("HTTP server started");

}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  handlePeltier();
  measureBattery();
  delay(50);
}

void handleCoolingMode0() {
  updateCoolingMode(0);
  showControlHtml();
}

void handleCoolingMode1() {
  updateCoolingMode(1);
  showControlHtml();
}

void handleCoolingMode2() {
  updateCoolingMode(2);
  showControlHtml();
}


void handleDisableLed() {
  ledEnabled = false;
  analogWrite(ledPin, 0);
  showControlHtml();
}

void handleDisableWifi() {
  webServer.close();
  WiFi.mode(WIFI_OFF);
  showControlHtml();
}

void showControlHtml() {
  webServer.send(200, "text/html", SendHTML(batteryLevel, coolingMode));
}

void updateCoolingMode(int cooling_mode){
  if(coolingMode != cooling_mode){
    coolingMode = cooling_mode;
    peltierOnTime = peltierOnTimeModes[coolingMode];
    peltierOffTime = peltierOffTimeModes[coolingMode];
    for (int i=0; i<2; i++){
      analogWrite(ledPin, 255);
      delay(50);
      analogWrite(ledPin, 0);
      delay(50);
    }
  }
}

void batteryLowWarning() {
  for (int i=0; i<3; i++){
    analogWrite(ledPin, 255);
    delay(200);
    analogWrite(ledPin, 0);
    delay(200);
  }
}

void handlePeltier() {
  if (peltierTimer < millis()) {
    if (batteryLevel > 0) {
      if (peltierState == 0) {      
        peltierTargetPwm = peltierPwmMax;      
        peltierState = 1;
        peltierTimer = millis() + peltierOnTime;
      } else {
        peltierTargetPwm = peltierPwmMin;
        peltierState = 0;
        peltierTimer = millis() + peltierOffTime;
      }
    }else{
      //battery too low
      peltierTargetPwm = 0;
      batteryLowWarning();
      peltierTimer = millis() + 10000;
    }
  }
  if (peltierTargetPwm != peltierCurrentPwm) {
    if (peltierTargetPwm > peltierCurrentPwm) {
      //if target is higher don't ease
      peltierCurrentPwm = peltierTargetPwm;
      //peltierCurrentPwm += 10;
    } else {
      //ease out when lowering
      peltierCurrentPwm -= 10;
    }
    if (peltierCurrentPwm > peltierPwmMax) {
      peltierCurrentPwm = peltierPwmMax;
    }
    if (peltierCurrentPwm < peltierPwmMin) {
      peltierCurrentPwm = peltierPwmMin;
    }
    if(ledEnabled == true){
      analogWrite(ledPin, map(peltierCurrentPwm, peltierPwmMin, peltierPwmMax, 0, 255));
    }
    analogWrite(peltierPin, peltierCurrentPwm);
    //Serial.println(peltierCurrentPwm);
  } else {
    //Serial.println(".");
  }
}

void measureBattery() {
  //only measure if peltier is not powered (or at min value) to avoid voltage fluctuations
  if(batteryMeasureTimer < millis()){
    if(peltierCurrentPwm <= peltierPwmMin){    
      int batteryReadingNew = analogRead(batteryPin);
      int batteryReading = batteryReadingAvg.reading(batteryReadingNew);  
      batteryLevel = map(batteryReading, voltageReadingMin, voltageReadingMax, 0, 100);
      if (batteryLevel > 100) {
        batteryLevel = 100;
      }
    }
    batteryMeasureTimer = millis() + batterySamplingTime;
  }
}

String getBatteryColor(int battery_level) {
  if (battery_level > 70) return "#4caf50";
  if (battery_level > 30) return "#ffc107";
  return "#f44336";
}

String SendHTML(int battery_level, int cooling_mode) {
  String ptr = "<!DOCTYPE html>\n";
  ptr += "<html>\n";
  ptr += "<head>\n";
  ptr += "<title>Personal Thermoregulator</title>\n";
  ptr += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  ptr += "<style>\n";
  ptr += "body { font-family: Arial, sans-serif; }\n";
  ptr += ".container { max-width: 500px; margin: 0 auto; padding: 20px; }\n";
  ptr += ".title { text-align: center; font-size: 24px; margin-bottom: 30px; }\n";
  ptr += ".label { text-align: center; font-size: 18px; margin-bottom: 10px; }\n";
  ptr += ".battery-container { margin-bottom: 20px; }\n";
  ptr += ".battery { display: flex; align-items: center; height: 30px; background-color: #ddd; position: relative; border-radius: 4px; overflow: hidden; margin-top: 10px; }\n";
  ptr += ".level { height: 100%; background-color: " + getBatteryColor(batteryLevel) + "; width: " + String(batteryLevel) + "%; transition: width 0.5s; }\n";
  ptr += ".battery-text { position: absolute; top: 50%; transform: translateY(-50%); right: 5px; color: #fff; font-weight: bold; }\n";
  //ptr += ".button-container { display: flex; align-items: center; margin-top: 30px; }\n";
  ptr += ".button-container { margin-bottom: 20px; margin-top: 10px; }\n";
  ptr += ".buttons { display: flex; align-items: center; justify-content: space-between;}\n";
  ptr += ".button { padding: 14px 26px; background-color: #ccc; color: #fff; border: none; border-radius: 4px; margin: 10px 0; cursor: pointer; }\n";
  ptr += ".button.active { background-color: #00bbff; }\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<div class=\"container\">\n";
  ptr += "<h1 class=\"title\">Personal Thermoregulator</h1>\n";
  ptr += "<div class=\"battery-container\">\n";
  ptr += "<span class=\"label\">Battery</span>\n";
  ptr += "<div class=\"battery\">\n";
  ptr += "<div class=\"level\" id=\"batteryLevel\"></div>\n";
  ptr += "<span class=\"battery-text\">" + String(battery_level) + "%</span>\n";
  ptr += "</div>\n";
  ptr += "</div>\n";
  ptr += "<div class=\"button-container\">\n";
  ptr += "<span class=\"label\">Cooling Mode</span>\n";
  ptr += "<div class=\"buttons\">\n";
  ptr += "<button class=\"button" + String(cooling_mode == 0 ? " active" : "") + "\" onclick=\"changeCoolingMode(0)\">Low</button>\n";
  ptr += "<button class=\"button" + String(cooling_mode == 1 ? " active" : "") + "\" onclick=\"changeCoolingMode(1)\">Medium</button>\n";
  ptr += "<button class=\"button" + String(cooling_mode == 2 ? " active" : "") + "\" onclick=\"changeCoolingMode(2)\">High</button>\n";
  //ptr += "<button class=\"button" + String(cooling_mode == 3 ? " active" : "") + "\" onclick=\"changeCoolingMode(3)\">Test</button>\n";
  ptr += "</div>\n";
  ptr += "</div>\n";
  ptr += "<div class=\"button-container\">\n";
  ptr += "<span class=\"label\">Options</span>\n";
  ptr += "<div class=\"buttons\">\n";
  ptr += "<button class=\"button\" onclick=\"disableLed()\">Disable Led</button>\n";
  ptr += "<button class=\"button\" onclick=\"disableWifi()\">Disable Wifi</button>\n";
  ptr += "</div>\n";
  ptr += "</div>\n";
  ptr += "<script>\n";
  ptr += "var batteryLevel = " + String(battery_level) + ";\n";
  ptr += "var coolingMode = " + String(cooling_mode) + ";\n";
  ptr += "function updateBatteryLevel() {\n";
  ptr += "var batteryLevelElement = document.getElementById('batteryLevel');\n";
  ptr += "batteryLevelElement.style.width = batteryLevel + '%';\n";
  ptr += "batteryLevelElement.style.backgroundColor = getBatteryColor(batteryLevel);\n";
  ptr += "}\n";
  ptr += "function disableLed() {\n";
  ptr += "location.href = '/disable_led';\n";
  ptr += "}\n";
   ptr += "function disableWifi() {\n";
  ptr += "location.href = '/disable_wifi';\n";
  ptr += "}\n";
  ptr += "function changeCoolingMode(mode) {\n";
  ptr += "coolingMode = mode;\n";
  ptr += "location.href = '/'+mode;\n";
  ptr += "highlightButton(mode);\n";
  ptr += "}\n";
  ptr += "function highlightButton(mode) {\n";
  ptr += "var buttons = document.getElementsByClassName('button');\n";
  ptr += "for (var i = 0; i < buttons.length; i++) {\n";
  ptr += "buttons[i].classList.remove('active');\n";
  ptr += "}\n";
  ptr += "buttons[mode].classList.add('active');\n";
  ptr += "}\n";
  ptr += "updateBatteryLevel();\n";
  ptr += "highlightButton(coolingMode);\n";
  ptr += "</script>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}
