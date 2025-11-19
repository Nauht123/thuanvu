//BEFORE RUNNING
//NECESSARY LIBRARY:
/*
- RTClib by Adafruit
- ESPAsyncWebSrv by dvarrel / or ESPAsyncWebServer by lacamera (need testing)
- Async TCP by ESP32 Async
- DHT sensor library by Adafruit
- AwslotWiFiClient by Danila....
- Adafruit Unified Sensor
- AdafruitJson by Benoit 
- ESP Mail Client by Mobizt

Chắc là thế, nhưng mà nếu như không được thì cài thêm mấy cái này, từng cái một, nếu được thì không cần cài thêm. 
- Arduino Uno WiFi Dev Ed Library by Arduino 
- ESPAsyncTCP by dvarrel
- Adafruit SSD1306
*/
//NECESSARY BOARD - go to board manager -> INSTALLED esp32 by Espressif -> VERSION 3.0.7 - NEWER VERSION WILL GET tcp_alloc ERROR
//BEFORE UPLOADING: Tools -> Partition Scheme -> NO OTA (2MB APP/2MB SPIFFS) - using Default will fuck up the memory
//There is a config.h file that contain personal's info configuration, change it before you run. 
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebSrv.h>
#include <SPIFFS.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <ESP_Mail_Client.h>
#include "config.h"
#include "esp_task_wdt.h"
#include <esp_log.h>

const int serverPort = 3000; // Backend port
const char* serverPath = "/history"; // Backend path

// Static IP config
IPAddress local_IP(192, 168, 1, 220); 
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// Pin - change if needed
#define RELAY_PIN 17
#define SOIL_MOISTURE_PIN 33 
#define DHTPIN 23
#define WATER_LEVEL_SENSOR 34
#define CLOCK_INTERRUPT_PIN GPIO_NUM_4
#define DHTTYPE DHT22

//mail service
#define ESP_MAIL_DEFAULT_FLASH_FS SPIFFS
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

// Global variables 
// - For data
float temperature = 0.0;
float humidity = 0.0;
float soilMoisture = 0.0;
int water = 0; //analog read for this one
String date = "0000-00-00";
String time0 = "00:00:00";

// - For controlling
//with RTC_DATA_ATTR is to save data in RTC memory instead of SRAM, preventing data loss after deep sleep
RTC_DATA_ATTR int plantID = 1; // Default (updated via /control)
RTC_DATA_ATTR int lowerThreshold = 30;
RTC_DATA_ATTR int upperThreshold = 60; 
bool isPumpOn = false; 
bool isManualMode = false;
RTC_DATA_ATTR bool dhtSent = false; //flag for sending email
RTC_DATA_ATTR bool rtcSent = false;
RTC_DATA_ATTR bool waterSent = false;
bool valid = false; //for checking data's validity
unsigned long pumpTimer = 0; //timer for pump - limiting watering time

const unsigned long INTERVAL = 60000; //data collection interval, can be changed
const unsigned long CHECK_INTERVAL = 2000; //for continuous check while pump is on 
const unsigned long PUMP_DURATION = 10000; //max duration for pump on in manual mode
const unsigned long SERVER_CHECK_INTERVAL = 10000; //constantly check for server availability and send saved data

//SPIFFS to store data
const char* DATA_FILE = "/data.json";
const size_t MAX_FILE_SIZE = 10*1024; //10 kB, can be adjusted

const uint64_t DEEP_SLEEP_DURATION = 1 * 3600 * 1000000ULL; // 1 hour in microsec
RTC_DATA_ATTR bool isDSMode = false; //Deep Sleep Mode

AsyncWebServer server(80);
DHT dht(DHTPIN, DHTTYPE);
RTC_DS3231 rtc; //object
SMTPSession smtp;
Session_Config config;

bool waitNetwork(uint8_t retries = 10) {
    uint8_t attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < retries) {
        Serial.print("Connecting to WiFi (Attempt ");
        Serial.print(attempt + 1);
        Serial.println(")...");
        delay(1000);
        attempt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        return true;
    }

    Serial.println("WiFi connection failed after retries");
    return false;
}

void smtpCallback(SMTP_Status status) {
    Serial.println(status.info());
    if (status.success()) {
        Serial.println("----------------");
        ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
        ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
        Serial.println("----------------\n");

        for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
            SMTP_Result result = smtp.sendingResult.getItem(i);
            ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
            ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
            ESP_MAIL_PRINTF("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
            ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
            ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
        }
        Serial.println("----------------\n");
        smtp.sendingResult.clear(); // Clear results to free memory
    }
}

void emailSetup() {
    MailClient.networkReconnect(true); //reconnection option
    smtp.debug(1); //0 for no debugging, 1 for basic debugging
    smtp.callback(smtpCallback);
     //for user defined session credentials 
    config.server.host_name = SMTP_HOST;
    config.server.port = SMTP_PORT;
    config.login.email = AUTHOR_EMAIL;
    config.login.password = AUTHOR_PASSWORD;
    config.login.user_domain = "";
    config.time.ntp_server = F("pool.ntp.org,time.nist.gov");
    config.time.gmt_offset = 7; //GMT + 7
    config.time.day_light_offset = 0;
}

void emailSending(const char* subject, const char* content) { //using char* since it requires c_str
    SMTP_Message message;
    message.sender.name = F("ESP32");
    message.sender.email = AUTHOR_EMAIL;
    message.subject = subject;
    message.addRecipient(F("HikariL3"), RECIPIENT_EMAIL);

    message.text.content = content;
    message.text.charSet = "utf-8";
    message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
    message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
    message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

    const int maxRetries = 3;
    int retries = 0;
    bool sent = false;

    while(retries < maxRetries && !sent) {
        if (!smtp.connect(&config)) {
            Serial.println(" Mail service connection error: " + smtp.errorReason());
            retries++;
            delay(2000);
            continue;
        }
        if (!smtp.isLoggedIn()) {
            Serial.println("\nNot yet logged in.");
        } else {
        if (smtp.isAuthenticated())
            Serial.println("\nSuccessfully logged in.");
        else
            Serial.println("\nConnected with no Auth.");
        }
        if (!MailClient.sendMail(&smtp, &message)) {
            Serial.println(" Mail service connection error: " + smtp.errorReason());
            retries++;
            delay(2000);
        } else {
            Serial.println("\nEmail sent successfully!");
            sent = true;
        }
    }
    if(!sent) {
    Serial.println("\nFailed to send email after " + String(maxRetries) + " retries");
    }
}

//deep sleep implementation
void IRAM_ATTR onAlarm() {
    //empty
}

void setDSAlarm(){
    rtc.disable32K(); //we dont use this
    rtc.writeSqwPinMode(DS3231_OFF);

    //clear existing alarms
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.disableAlarm(2);

    //set alarm 1 to trigger at 00:00:00 everyday
    DateTime now = rtc.now();
    isDSMode = (now.hour() >= 0 && now.hour() < 5);
    DateTime alarmTime = DateTime(now.year(), now.month(), now.day(), 0, 0, 0);
    if (now.hour() >= 0 && now.hour() < 5) {
        isDSMode = true;
    } else {
        if (now.hour() >= 5) {
            alarmTime = alarmTime + TimeSpan(1, 0, 0, 0); //next day
        }
    }
    if (!rtc.setAlarm1(alarmTime, DS3231_A1_Hour)) {
        Serial.println("Error setting daily alarm");
    } else {
        Serial.println("Alarm set for " + String(alarmTime.year()) + "-" 
        + String(alarmTime.month()) + "-" + String(alarmTime.day()) + " " 
        + String(alarmTime.hour()) + ":00:00");
    }
 
    //configure SQW pin for interrupt
    pinMode(CLOCK_INTERRUPT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(CLOCK_INTERRUPT_PIN), onAlarm, FALLING);
}

void enterDeepSleep() {
    Serial.println("Entering deep sleep for 1 hour...");
    DateTime now = rtc.now();
    if (now.hour() < 4) { //Set alarm for next hour if before 04:00
        DateTime nextAlarm = DateTime(now.year(), now.month(), now.day(), now.hour() + 1, 0, 0);
        if (!rtc.setAlarm1(nextAlarm, DS3231_A1_Hour)) {
            Serial.println("Error setting hourly alarm");
        } else {
            Serial.println("Hourly alarm set for " + String(nextAlarm.hour()) + ":00:00");
        }  
    }
    esp_sleep_enable_ext0_wakeup(CLOCK_INTERRUPT_PIN, 0); // wake on LOW (SQW is active LOW)
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION); // fallback timer 
    esp_deep_sleep_start();
}

//reader
float readSoilMois(){
    int value = analogRead(SOIL_MOISTURE_PIN);
    float soilPercentage = map(value, 1100, 4095, 100, 0);
    if (soilPercentage < 0) soilPercentage = 0;
    if (soilPercentage > 100) soilPercentage = 100;
    return soilPercentage;
}

bool getTime(String &date, String &time0) {
    DateTime now = rtc.now();
     if(!now.isValid()){
        Serial.println("Failed to obtain time from DS3231");
        if(!rtcSent && WiFi.status() == WL_CONNECTED) {
            emailSending("[DS3231 FAILURE]", "Please check your DS3231 wiring and its power. The system will fail without this sensor!");
            rtcSent = true;
            Serial.println("Email notification for DS3231 failure sent");
        }
        return false;
     } else {
        if(rtcSent){
            Serial.println("DS3231 fixed");
            rtcSent = false;
        }
     }
     //FORMAT AS YYYY-MM-DD
     char dateStr[11];
     snprintf(dateStr, sizeof(dateStr),"%04d-%02d-%02d", now.year(),now.month(), now.day());
     date = String(dateStr);
     //FORMAT AS HH:MM:SS
     char timeStr[9];
     snprintf(timeStr, sizeof(timeStr),"%02d:%02d:%02d", now.hour(), now.minute(),now.second());
     time0 = String(timeStr);
     return true;
}

bool waterLevel(){
    water = analogRead(WATER_LEVEL_SENSOR);
    if(water < 1400){
        if(!waterSent && WiFi.status() == WL_CONNECTED) {
            emailSending("[LOW ON WATER]", "Please refill the water for the pump to run.");
            waterSent = true;
            Serial.println("Email notification for water refill sent");
        }
        return false; 
    } else {
        if(waterSent){
            Serial.println("Water refilled!");
            waterSent = false;
            }
        }
    return true;
}

void collectData(){
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    soilMoisture = readSoilMois();
    valid = true;

    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Failed to read from DHT sensor!");
        //temperature = 0.0; //fallback value
        //humidity = 0.0;
        valid = false;
        if(!dhtSent && WiFi.status() == WL_CONNECTED) {
            emailSending("[DHT FAILURE]", "Please check your DHT22 wirings");
            dhtSent = true;
        }
    } else {
        if(dhtSent){
            Serial.println("DHT22 fixed");
            dhtSent = false;
        }
    }
    if (!getTime(date, time0)) {
        Serial.println("Failed to read from DS3231");
        // date = "0000/00/00"; //fallback value
        // time0 = "00:00:00";
        valid = false;
    }
}

void controlAutomatic(){
    if (isManualMode) return;
    soilMoisture = readSoilMois();
    if(!waterLevel()){
        Serial.println("Water level is too low, cancel watering");
        if(isPumpOn){
            isPumpOn = false;
            digitalWrite(RELAY_PIN, LOW);
            return;
        }
    }
    if (soilMoisture < lowerThreshold && !isPumpOn) {
        digitalWrite(RELAY_PIN, HIGH);
        isPumpOn = true;
        Serial.println("Pump turned ON (Automatic): Soil moisture (" + String(soilMoisture) + ") below lower threshold (" + String(lowerThreshold) + ")");
    } 
    else if (soilMoisture > (upperThreshold - 15) && isPumpOn) {
        digitalWrite(RELAY_PIN, LOW);
        isPumpOn = false;
        collectData();
        processData();
        Serial.println("Pump turned OFF (Automatic): Soil moisture (" + String(soilMoisture) + ")");
    }
}

void controlManual(){
    static unsigned long lastCheck = 0; 
    unsigned long current = millis();
    if(current - lastCheck < CHECK_INTERVAL) return;
    lastCheck = current;
    if(!waterLevel()){
        Serial.println("Water level is too low, cancel watering");
        if(isPumpOn){
            isPumpOn = false;
            isManualMode = false;
            digitalWrite(RELAY_PIN, LOW);
            return;
        }
    }
    collectData();
    if(!valid) return;
    String payload = "{\"plantID\": \"" + String(plantID) + "\", " +
                         "\"soilMoisture\": " + String(soilMoisture, 2) + ", " +
                         "\"temperature\": " + String(temperature, 2) + ", " +
                         "\"airMoisture\": " + String(humidity, 2) + ", " +
                         "\"date\": \"" + date + "\", " +
                         "\"time\": \"" + time0 + "\"}";

    if (WiFi.status() == WL_CONNECTED) {
        if (sendData(payload)) {
            Serial.println("Manual mode: Data sent to server");
        } 
    }
    if (current - pumpTimer > PUMP_DURATION ) {
        digitalWrite(RELAY_PIN, LOW);
        isPumpOn = false;
        isManualMode = false;
        Serial.println("Pump turned OFF (Manual): Max duration reached.");
    }
}

bool serverOn() { //check server availability for sending saved data
    WiFiClient client;
    if (!client.connect(serverHost, serverPort, 2000)) { 
        return false;
    }
    client.stop(); 
    return true;
}

//data processing
void processData(){
    if(!valid) return;
    String payload = "{\"plantID\": \"" + String(plantID) + "\", " +
                         "\"soilMoisture\": " + String(soilMoisture, 2) + ", " +
                         "\"temperature\": " + String(temperature, 2) + ", " +
                         "\"airMoisture\": " + String(humidity, 2) + ", " +
                         "\"date\": \"" + date + "\", " +
                         "\"time\": \"" + time0 + "\"}";

    if (WiFi.status() == WL_CONNECTED) {
        if(sendData(payload)){
        Serial.println("Data sent successfully");
        sendSaved();
        } else {
            Serial.println("Failed to connect to server, saving data...");
            saveData(payload);
        } 
    } else {
        Serial.println("WiFi disconnected, reconnecting...");
        WiFi.reconnect();
        if (waitNetwork(15)) {
            Serial.print("Reconnected to WiFi: ");
            Serial.println(WiFi.localIP());
            if(sendData(payload)){
                Serial.println("Data sent successfully");
                sendSaved();
            } else {
        Serial.println("Failed to connect to server after reconnect, saving data...");
        saveData(payload);
            }
        } else {
        Serial.println("Failed to reconnect to WiFi, saving data...");
        saveData(payload);
        }
    }
}

void saveData(const String& payload){
    File file = SPIFFS.open(DATA_FILE, FILE_APPEND);
    if(!file){
        //Failed to open file for appending
        return; 
    }
    //check file's size
    size_t fileSize = file.size();
     if(fileSize + payload.length() > MAX_FILE_SIZE){
        Serial.println("File size exceeds limit, deleting oldest entries...");
        file.close();
        //read entries
        File readFile = SPIFFS.open(DATA_FILE, FILE_READ);
        if(!readFile){
            //Failed to open file for reading
            return;
        }
        String allData = readFile.readString();
        readFile.close();
        //split into lines
        int newlineIndex = allData.indexOf('\n');
        if(newlineIndex == -1){
            Serial.println("No data to delete, overwriting file...");
            SPIFFS.remove(DATA_FILE);
            file = SPIFFS.open(DATA_FILE, FILE_WRITE);
        }
        else {
            //remove oldest entry / 1st line
            allData = allData.substring(newlineIndex + 1);
            SPIFFS.remove(DATA_FILE);
            file = SPIFFS.open(DATA_FILE, FILE_WRITE);
            file.print(allData);
        }
     }
     file.println(payload);
     file.close();
     Serial.println("Data saved to SPIFFS:" + payload);
}

bool sendData(const String& payload){
    WiFiClient client;
    if(!client.connect(serverHost, serverPort, 5000)){
        Serial.println("failed to connect to server");
        return false;
    }
    client.println(String("POST ") + serverPath + " HTTP/1.1");
    client.println(String("Host: ") + serverHost + ":" + serverPort);
    client.println("Content-Type: application/json");
    client.println(String("Content-Length: ") + payload.length());
    client.println();
    client.println(payload);

    unsigned long startTime = millis();
    while (client.connected() && millis() - startTime < 5000){
        if(client.available()){
            String line = client.readStringUntil('\n');
            Serial.println(line);
        }
    }
    client.stop();

    Serial.println("HTTP request sent successfully");
    return true;
}

void sendSaved(){
    File file = SPIFFS.open(DATA_FILE, FILE_READ);
    if(!file){
        //No saved data to send
        return;
    }

    while(file.available()){
        String line = file.readStringUntil('\n');
        line.trim();
        if(line.length() > 0){
            if(sendData(line)){
                Serial.println("Sent saved data: " + line);
            } else {
                Serial.println("failed to send saved data, keeping in SPIFFS: " + line);
                saveData(line);
                while(file.available()){
                    String remain = file.readStringUntil('\n');
                    remain.trim();
                    if(remain.length() > 0){
                        saveData(remain);
                    }
                }
                break;
            }
        }
    }
    file.close();
    File checkFile = SPIFFS.open(DATA_FILE, FILE_READ);
    if (!SPIFFS.exists(DATA_FILE) || checkFile && checkFile.size() == 0){
        checkFile.close();
        return;
    }
    if (checkFile) checkFile.close();
    //SPIFFS.remove(DATA_FILE);
    Serial.println("Cleared saved data from SPIFFS");
}

void setup() {
    Serial.begin(115200);
    esp_log_level_set("task_wdt", ESP_LOG_NONE);
    esp_task_wdt_deinit();
    // SENSOR INIT SECTION
    // init pin
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // Pump off 
    isPumpOn = false;

    // init DHT
    dht.begin();

    //init RTC and SPIFFS
    if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    ESP.restart();
    }

    if (!rtc.begin()) {
    Serial.println("Couldn't find DS3231 RTC");
    while (1) delay (100); // Halt if RTC not found
    }

    if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // WIFI INIT SECTION
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
        Serial.println("Failed to configure static IP");
    }

    // Connect to WiFi
    WiFi.begin(ssid, password);
    if (!waitNetwork(15)) {
        Serial.println("Network setup failed, restarting...");
        ESP.restart();
    }
    //DEEP SLEEP SET UP
    setDSAlarm();

    //EMAIL SET UP
    emailSetup();

    //CORS headers 
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Handle CORS preflight requests (OPTIONS) for all paths
    server.on(".*", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
        Serial.println("Received OPTIONS request for CORS preflight");
        request->send(200, "text/plain", "");
    });

    // Handle POST requests to /control (Automatic Mode configuration)
    server.on("/control", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("body", true)) {
            Serial.println("Error: No body parameter in /control request");
            request->send(400, "application/json", "{\"error\":\"Missing body parameter\"}");
            return;
        }

        String body = request->getParam("body", true)->value();
        Serial.println("Received /control request: " + body);

        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            Serial.println("Error: Failed to parse JSON - " + String(error.c_str()));
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (!doc.containsKey("plantID") || !doc.containsKey("lowerThreshold") || !doc.containsKey("upperThreshold")) {
            Serial.println("Error: Missing required fields in /control request");
            request->send(400, "application/json", "{\"error\":\"Missing required fields\"}");
            return;
        }

        plantID = doc["plantID"];
        lowerThreshold = doc["lowerThreshold"];
        upperThreshold = doc["upperThreshold"];

        Serial.print("Plant ID: ");
        Serial.println(plantID);
        Serial.print("Lower Threshold: ");
        Serial.println(lowerThreshold);
        Serial.print("Upper Threshold: ");
        Serial.println(upperThreshold);
        Serial.println("Automatic mode configured for Plant ID: " + String(plantID));

        request->send(200, "application/json", "{\"message\":\"Automatic mode configured\"}");
    });

    // Handle POST requests to /pump (Manual Mode - Forced Watering)
    server.on("/pump", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("body", true)) {
            Serial.println("Error: No body parameter in /pump request");
            request->send(400, "application/json", "{\"error\":\"Missing body parameter\"}");
            return;
        }

        String body = request->getParam("body", true)->value();
        Serial.println("Received /pump request: " + body);

        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            Serial.println("Error: Failed to parse JSON - " + String(error.c_str()));
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (!doc.containsKey("pumpState")) {
            Serial.println("Error: Missing pumpState field in /pump request");
            request->send(400, "application/json", "{\"error\":\"Missing pumpState field\"}");
            return;
        }

        const char* pumpState = doc["pumpState"];
        if (strcmp(pumpState, "on") == 0) {
            digitalWrite(RELAY_PIN, HIGH);
            isPumpOn = true;
            isManualMode = true;
            pumpTimer = millis();
            Serial.println("Pump turned ON (Manual)");
        } else if (strcmp(pumpState, "off") == 0) {
            digitalWrite(RELAY_PIN, LOW);
            isPumpOn = false;
            isManualMode = false;
            Serial.println("Pump turned OFF (Manual)");
        } else {
            Serial.println("Error: Invalid pump state - " + String(pumpState));
            request->send(400, "application/json", "{\"error\":\"Invalid pump state\"}");
            return;
        }

        request->send(200, "application/json", "{\"message\":\"Pump command executed\"}");
    });

    server.begin();
    Serial.println("Web server started");
}

void loop() {
    DateTime now = rtc.now();
    if (rtc.alarmFired(1)) {
        rtc.clearAlarm(1);
    }
    if (now.hour() >= 0 && now.hour() < 5) {
        isDSMode = true;
    } else {
        isDSMode = false;
    }

    if (isDSMode) {
        collectData();
        controlAutomatic();
        processData();
        enterDeepSleep(); 
    }

    static unsigned long lastCollection = 0;
    static unsigned long lastServer = 0; //used to check if server is on to send data without waiting for next data connection.
    unsigned long current = millis();

    if (!isPumpOn) {
        if (current - lastServer >= SERVER_CHECK_INTERVAL && WiFi.status() == WL_CONNECTED){
            File checkFile = SPIFFS.open(DATA_FILE, FILE_READ);
            if (checkFile && checkFile.size() > 0) {
                checkFile.close();
                if (serverOn()){
                    sendSaved();
                }
            } else if (checkFile) {
            checkFile.close();
            }
            lastServer = current;                                 //me when I see 727, lmao .______.
        }
        if (current - lastCollection >= INTERVAL) {
            collectData();
            controlAutomatic();
            processData(); // Send or save
            if(valid)
            lastCollection = current;
        }
    } else {
        if (isManualMode) {
            controlManual(); // Includes sending
        } else {
            static unsigned long lastCheck = 0;
            if (current - lastCheck >= CHECK_INTERVAL) {
                lastCheck = current;
                controlAutomatic(); // no continuous sending
            }
        }
    } 
     delay(100); //avoid tight loop
}

/* 
To-do list:
- Wire DS3231 and water level sensor (done)
- Check DS3231 and water level functionality (done)
- While watering, continuously track soil moisture (no need to send data, send when pump is off) instead of waiting for the next record (done)
- Add error handling for DHT22 and DS3231 instead of using fallback value (done)
- Warning when water is running low (done)
- IMPLEMENT GRAPH DATA ON SERVER SIDE (done)
Further enhancement: (if time allows)
- Make a simple model (Done)
*/