/*
 * main.h
 *
 *  Created on: Feb 29, 2020
 *      Author: prageeth
 */

#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Ticker.h>
#include <ArduinoOTA.h>

#define RXD2 16
#define TXD2 17

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define FMT_BASIC_INFO "[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]"
#define FMT_VOLTAGES "[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]"
#define CONTENT_TYPE_APPLICATION_JSON "application/json"

#define MIN_BATT_VOLTAGE 4000
#define MAX_BATT_VOLTAGE 6200
#define MAX_NOMINAL_CAPACITY 200000

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	char *array;
	volatile unsigned int used;
	volatile unsigned int size;
} Array;

void setupPins();
void bmsWrite(char *data, int len);
void bmsRead(Array *a);
void initBms();
void initBmsStub(char * data, int len);
void bmsDrainSerial();
bool setupWiFi();
void configureWiFi();
void setupWebServer();
void notFound(AsyncWebServerRequest *request);
void setupNTPClient();
void setupOTA();
void handleRoot();
void writeFets(char s);
void bmsv();
void bmsb();
void initArray(Array *a, unsigned int initialSize);
void insertArray(Array *a, char element);
void freeArray(Array *a);
void printCharArrayHex(Array *a);
String readProperty(String props, String key);

#ifdef __cplusplus
} // extern "C"
#endif
#endif /* SRC_MAIN_H_ */
