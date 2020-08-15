#include "main.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
AsyncWebServer server(80); // Create a webserver object that listens for HTTP request on port 80
Ticker ticker;

volatile unsigned int ticks = 0;
volatile unsigned int totalTicks = 0;
volatile unsigned long int epochWiFi = 0;
volatile unsigned long int lastSerialCallAt = 0;
volatile bool bmsInit = false;
volatile bool bmsFet = false;
volatile char bmsFetVal = 0;
volatile unsigned int loops = 0;
String sv;
String sb;
volatile int current;
volatile uint8_t ota_loops = 0;

void initArray(Array *a, unsigned int initialSize) {
	a->used = 0;
	a->array = (char*) malloc(initialSize * sizeof(char));
	a->size = initialSize;
}

void insertArray(Array *a, char element) {
	if (a->used == a->size) {
		a->size *= 2;
		a->array = (char*) realloc(a->array, a->size * sizeof(char));
	}
	a->array[a->used++] = element;
}

void freeArray(Array *a) {
	printCharArrayHex(a);
	free(a->array);
	a->array = NULL;
	a->used = a->size = 0;
}

void ISRwatchdog() {
	++totalTicks;
	if (++ticks > 30) {
		ESP.restart();
	}
}

void setup(void) {
	Serial.begin(115200);
	delay(100);
	Serial.println("Starting");
	ticker.attach(1, ISRwatchdog);
	Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
	setupPins();
	initBms();
	if (!SPIFFS.begin()) {
		Serial.println("Error setting up SPIFFS!");
	}
	if (setupWiFi()) {
		initBms();
		setupWebServer();
		setupNTPClient();
		setupOTA();
		Serial.print("Connected to ");
		Serial.println(WiFi.SSID());  // Tell us what network we're connected to
		Serial.print("IP address:\t");
		Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
		initBms();
	}
}

void loop(void) {
	ticks = 0;
	ArduinoOTA.handle();
	if (timeClient.getEpochTime() - epochWiFi > 20) {
		Serial.println("Checking WiFi connectivity.");
		epochWiFi = timeClient.getEpochTime();
		if (!WiFi.isConnected()) {
			ESP.restart();
		}
		if (!timeClient.update()) {
			ESP.restart();
		}
	}
	if (bmsInit) {
		bmsInit = false;
		bmsv();
		bmsb();
	} else if (bmsFet) {
		bmsFet = false;
		writeFets(bmsFetVal);
	}
	if (loops % 10000 == 1) {
		bmsInit = true;
	}
	loops++;
}

void setupPins() {
}

String readProperty(String props, String key) {
	int index = props.indexOf(key, 0);
	if (index != -1) {
		int start = index + key.length();
		start = props.indexOf('=', start);
		if (start != -1) {
			start = start + 1;
			int end = props.indexOf('\n', start);
			String value = props.substring(start,
					end == -1 ? props.length() : end);
			value.trim();
			return value;
		}
	}
	return "";
}

bool setupWiFi() {

	configureWiFi();
	if (SPIFFS.exists("/firmware.properties")) {
		File f = SPIFFS.open("/firmware.properties", "r");
		if (f && f.size()) {
			Serial.println("Reading firmware.properties");
			String props = "";
			while (f.available()) {
				props += char(f.read());
			}
			f.close();

			String ssid = readProperty(props, "wifi.ssid");
			String password = readProperty(props, "wifi.password");
			Serial.printf(
					"firmware properties wifi.ssid: %s; wifi.password: %s\r\n",
					ssid.c_str(), password.c_str());
			WiFi.begin(ssid.c_str(), password.c_str());

			Serial.printf("Connecting");
			while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
				ticks = 0;
				Serial.print('.');
				delay(40);
			}
			Serial.println();
			return true;
		}
	}
	return false;
}

void configureWiFi() {
	WiFi.persistent(false);
	WiFi.disconnect(true);
	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_STA);
	WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
		Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
	},
	WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);

	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
		WiFi.persistent(false);
		WiFi.disconnect(true);
		ESP.restart();
	},
	WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);
}

void setupWebServer() {
	sv = "";
	sb = "";

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/index.htm");
	});
	server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/index.js");
	});
	server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/favicon.ico");
	});
	server.on("/r", HTTP_GET, [](AsyncWebServerRequest *request) {
		char buf[256];
		sprintf(buf, "{\"v\":%s,\"b\":%s}", sv.c_str(), sb.c_str());
		AsyncWebServerResponse *response = request->beginResponse(200,
		CONTENT_TYPE_APPLICATION_JSON, buf);
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
	server.on("/c", HTTP_GET, [](AsyncWebServerRequest *request) {
		char buf[8];
		sprintf(buf, "%d", current);
		AsyncWebServerResponse *response = request->beginResponse(200,
		CONTENT_TYPE_APPLICATION_JSON, buf);
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
	server.on("/w", HTTP_POST, [](AsyncWebServerRequest *request) {
		try {
			AsyncWebParameter *s = request->getParam("s", true, false);
			bmsFetVal = s->value().toInt();
			bmsFet = true;
			AsyncWebServerResponse *response = request->beginResponse(200,
					CONTENT_TYPE_APPLICATION_JSON, "0");
					response->addHeader("Access-Control-Allow-Origin", "*");
			response->addHeader("Access-Control-Allow-Origin", "*");
			request->send(response);
		} catch (...) {
			Serial.println("exception");
			request->send_P(200, CONTENT_TYPE_APPLICATION_JSON, "-1");
		}
	});

	server.onNotFound(notFound);

	server.begin();
	Serial.println("HTTP server started");
}

void notFound(AsyncWebServerRequest *request) {
	request->send(SPIFFS, "/index.htm");
}

void setupNTPClient() {
	Serial.println("Synchronizing time with NTP");
	timeClient.begin();
	timeClient.setTimeOffset(39600);
	if (!timeClient.update()) {
		Serial.println("NTP update failed");
	}
	Serial.println(timeClient.getFormattedDate());
}

void setupOTA() {

	Serial.println("Setting up OTA");
	ArduinoOTA.setPassword("Sup3rSecr3t");
	ArduinoOTA.onStart([]() {
		Serial.println("Start");
		bmsDrainSerial();
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		if (ota_loops++ % 20 == 0)
			bmsv();
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR)
			Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR)
			Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR)
			Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR)
			Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR)
			Serial.println("End Failed");
	});
	ArduinoOTA.begin();
}

void writeFets(char s) {
	if (s < 0 || s > 3) {
		return;
	}

	// 0b11: discharge off, charge off
	// 0b10: discharge off, charge  on
	// 0b01: discharge  on, charge off
	// 0b00: discharge  on, charge  on

	char start[9] = { 0xdd, 0x5a, 0x00, 0x02, 0x56, 0x78, 0xff, 0x30, 0x77 };
	char end[9] = { 0xdd, 0x5a, 0x01, 0x02, 0x00, 0x00, 0xff, 0xfd, 0x77 };
	char checksum = 29 - s;
	char data[9] = { 0xdd, 0x5a, 0xe1, 0x02, 0x00, s, 0xff, checksum, 0x77 };
	bmsWrite(start, 9);
	bmsDrainSerial();
	bmsWrite(data, 9);
	bmsDrainSerial();
	bmsWrite(end, 9);
	bmsDrainSerial();
}

void bmsv() {

	char *buf = (char*) malloc(72 * sizeof(char));
	char data[7] = { 0xdd, 0xa5, 0x04, 0x00, 0xff, 0xfc, 0x77 };
	Array av;
	initArray(&av, 35);
	bmsWrite(data, 7);
	bmsRead(&av);
	if (av.used < 5 || av.array[0] != 0xdd || av.array[av.used - 1] != 0x77) {
		freeArray(&av);
		free(buf);
		buf = NULL;
		return;
	}
	unsigned int dataLen = av.array[3];
	dataLen |= av.array[2] << 8;
	if (dataLen > 64) {
		freeArray(&av);
		free(buf);
		buf = NULL;
		return;
	}
	buf[71] = 0;
	int cells[14];
	for (int x = 0; x < 14; x++) {
		cells[x] = (av.array[(x * 2) + 4] << 8) | av.array[(x * 2) + 5];
	}

	for (int x = 0; x < 14; x++) {
		if (cells[x] > 5000 || cells[x] < 2000) {
			freeArray(&av);
			free(buf);
			buf = NULL;
			return;
		}
	}
	sprintf(buf, FMT_VOLTAGES, cells[0], cells[1], cells[2], cells[3], cells[4],
			cells[5], cells[6], cells[7], cells[8], cells[9], cells[10],
			cells[11], cells[12], cells[13]);
	freeArray(&av);
	sv = String(buf);
	free(buf);
	buf = NULL;
}

void bmsb() {

	char *buf = (char*) malloc(100 * sizeof(char));
	char data[] = { 0xdd, 0xa5, 0x03, 0x00, 0xff, 0xfd, 0x77 };
	Array ab;
	initArray(&ab, 34);
	bmsWrite(data, 7);
	bmsRead(&ab);
	if (ab.used < 5 || ab.array[0] != 0xdd || ab.array[ab.used - 1] != 0x77) {
		freeArray(&ab);
		free(buf);
		buf = NULL;
		return;
	}
	unsigned int dataLen = ab.array[3];
	dataLen |= ab.array[2] << 8;
	if (dataLen > 64) {
		freeArray(&ab);
		free(buf);
		buf = NULL;
		return;
	}
	unsigned int voltage = (ab.array[4] << 8) | ab.array[5];
	unsigned int remainingCapacity = (ab.array[8] << 8) | ab.array[9];
	unsigned int nominalCapacity = (ab.array[10] << 8) | ab.array[11];
	if (voltage < MIN_BATT_VOLTAGE || voltage > MAX_BATT_VOLTAGE
			|| nominalCapacity > MAX_NOMINAL_CAPACITY
			|| remainingCapacity > MAX_NOMINAL_CAPACITY) {
		free(buf);
		buf = NULL;
		freeArray(&ab);
		return;
	}
	unsigned int ntc0 = (ab.array[27] << 8) | ab.array[28];
	unsigned int ntc1 = (ab.array[29] << 8) | ab.array[30];
	if (ntc0 == 0 || ntc1 == 0) {
		free(buf);
		buf = NULL;
		freeArray(&ab);
		return;
	}
	unsigned int curr = (ab.array[6] << 8) | ab.array[7];
	current = curr > 0x7fff ? (-1.0 * (0xffff - curr)) : curr;

	buf[99] = 0;
	uint8_t fets = ab.array[24];
	fets = ~fets & 0b11;
	sprintf(buf, FMT_BASIC_INFO,
			voltage, // Voltage
			current,   // Current
			remainingCapacity, nominalCapacity,
			(ab.array[12] << 8) | ab.array[13], // cycle times
			(ab.array[14] << 8) | ab.array[15], // date of manufacture
			ab.array[16] << 8 | ab.array[17], // cell balance state 1s-16s
			ab.array[18], ab.array[19], // cell balance state 17s-32s
			ab.array[20], ab.array[21], // protection state
			ab.array[22], // software version
			ab.array[23], // Percentage of remaining capacity
			fets, // MOSFET control status
			ab.array[25], // battery serial number
			ab.array[26], // Number of NTCs
			ntc0, // NTC 0, high bit first, Using absolute temperature transmission, 2731+ (actual temperature *10), 0 degrees = 2731, 25 degrees = 2731+25*10 = 2981
			ntc1); // NTC 1, high bit first, Using absolute temperature transmission, 2731+ (actual temperature *10), 0 degrees = 2731, 25 degrees = 2731+25*10 = 2981
	freeArray(&ab);
	sb = String(buf);
	free(buf);
	buf = NULL;
}

void bmsWrite(char *data, int len) {
	int x = 0;
	while (x < len) {
		Serial2.print((char) data[x++]);
	}
	lastSerialCallAt = timeClient.getEpochTime();
}

void bmsRead(Array *a) {
	unsigned int c = 0;
	while (!Serial2.available()) {
		if (++c > 0xffff) {
			break;
		}
	}
	while (Serial2.available()) {
		insertArray(a, Serial2.read());
	}
}

void initBms() {
	Serial.println("==== BMS Init...");
	char buf0[7] = { 221, 165, 4, 0, 255, 252, 119 };

	initBmsStub(buf0, 7);
	Serial.println("==== BMS init done.\r\n");
}

void initBmsStub(char *data, int len) {
	bmsWrite(data, len);
	Array a;
	initArray(&a, 8);
	bmsRead(&a);
	Serial.printf("==== a.used: %d\r\n", a.used);
	freeArray(&a);
}

void bmsDrainSerial() {
	Array a;
	initArray(&a, 32);
	bmsRead(&a);
	Serial.printf("drain: ");
	freeArray(&a);
}

void printCharArrayHex(Array *a) {
	unsigned int x = 0;
	while (x < a->used) {
		Serial.printf("0x%02x, ", a->array[x++]);
	}
	Serial.println();
}
