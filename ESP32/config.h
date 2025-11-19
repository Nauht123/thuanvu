//before running, copy this file or rename this file, set its name to "config.h"
//change below info to run

#ifndef CONFIG_H
#define CONFIG_H

//mail service
#define AUTHOR_EMAIL "SENDING_EMAIL"
#define AUTHOR_PASSWORD "SENDER'S_APP_PASSWORD"
#define RECIPIENT_EMAIL "RECIPIENT_EMAIL"

const char* ssid = "YOUR_WIFI_NETWORK"; //change to the wifi your laptop is using - 2.4GHz only
const char* password = "WIFI_PASSWORD"; //same as above
const char* serverHost = "192.168.1.XXX"; // Backend host - CHANGE TO MATCH YOUR CURRENT IP

#endif