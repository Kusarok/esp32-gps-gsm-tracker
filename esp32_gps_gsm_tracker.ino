#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define GPS_RX_PIN 32
#define GPS_TX_PIN 33

#define GSM_RX_PIN 26
#define GSM_TX_PIN 27

#define GPS_BAUD 115200

#define GPS_SERVER_HOST "SERVER_HOST"
const char* GPS_SERVER_IPS[] = {"SERVER_IP_1", "SERVER_IP_2"};
const int NUM_SERVERS = 2;
#define GPS_SERVER_PORT 22945
#define GPS_DEVICE_ID "DEVICE_ID"

#define APN "YOUR_APN"
#define APN_USER "YOUR_APN_USER"
#define APN_PASS "YOUR_APN_PASSWORD"

#define SEND_INTERVAL_MS 15000
#define LIVE_TRACKING_ENABLED true

unsigned long lastSendTime = 0;
bool gprsConnected = false;

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial gsmSerial(1);

String decodeUCS2(String hexStr) {
  String result = "";
  hexStr.trim();

  for (int i = 0; i < hexStr.length(); i += 4) {
    if (i + 4 <= hexStr.length()) {
      String hexChar = hexStr.substring(i, i + 4);
      long charCode = strtol(hexChar.c_str(), NULL, 16);

      if (charCode >= 32 && charCode <= 126) {
        result += (char)charCode;
      }
    }
  }
  return result;
}

bool isUCS2(String str) {
  if (str.length() < 4 || str.length() % 4 != 0) return false;

  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (!isHexadecimalDigit(c)) return false;
  }
  return true;
}

String readGSMResponse(unsigned long timeout = 2000) {
  String response = "";
  unsigned long startTime = millis();

  while (millis() - startTime < timeout) {
    while (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
    }
    delay(10);
  }
  return response;
}

String sendATCommand(String command, unsigned long timeout = 2000) {
  while (gsmSerial.available()) {
    gsmSerial.read();
  }

  gsmSerial.println(command);
  String response = readGSMResponse(timeout);

  return response;
}

unsigned long getUnixTimestamp() {
  if (!gps.date.isValid() || !gps.time.isValid()) {
    return 1734307200UL + (millis() / 1000);
  }

  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();
  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();

  unsigned long days = 0;

  for (int y = 1970; y < year; y++) {
    if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
      days += 366;
    } else {
      days += 365;
    }
  }

  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    daysInMonth[1] = 29;
  }

  for (int m = 1; m < month; m++) {
    days += daysInMonth[m - 1];
  }

  days += day - 1;

  unsigned long timestamp = days * 86400UL + hour * 3600UL + minute * 60UL + second;
  return timestamp;
}

bool initGPRS() {
  String response;

  Serial.println("\n--- Initializing GPRS Connection ---");

  sendATCommand("AT+CIPSHUT", 3000);
  delay(1000);

  response = sendATCommand("AT+CIPMUX=0", 2000);
  if (response.indexOf("OK") == -1) {
    Serial.println("[ERROR] CIPMUX failed");
    return false;
  }
  delay(500);

  String apnCmd = "AT+CSTT=\"" + String(APN) + "\",\"" + String(APN_USER) + "\",\"" + String(APN_PASS) + "\"";
  response = sendATCommand(apnCmd, 3000);
  if (response.indexOf("OK") == -1) {
    Serial.println("[ERROR] APN setup failed");
    return false;
  }
  delay(1000);

  Serial.println("Bringing up wireless connection...");
  response = sendATCommand("AT+CIICR", 10000);
  if (response.indexOf("OK") == -1 && response.indexOf("ERROR") != -1) {
    Serial.println("[ERROR] CIICR failed - Check SIM card data plan");
    return false;
  }
  delay(2000);

  response = sendATCommand("AT+CIFSR", 3000);
  if (response.indexOf("ERROR") != -1 || response.length() < 7) {
    Serial.println("[ERROR] Failed to get IP address");
    return false;
  }
  Serial.println("[OK] Got IP: " + response);

  gprsConnected = true;
  Serial.println("[OK] GPRS Connected Successfully!\n");
  return true;
}

bool sendHTTPRequest(String url, String serverIp) {
  String response;

  String connectCmd = "AT+CIPSTART=\"TCP\",\"" + serverIp + "\",\"" + String(GPS_SERVER_PORT) + "\"";
  response = sendATCommand(connectCmd, 10000);

  if (response.indexOf("CONNECT OK") == -1 && response.indexOf("ALREADY CONNECT") == -1) {
    Serial.println("[ERROR] TCP connection failed");
    sendATCommand("AT+CIPCLOSE", 2000);
    return false;
  }
  delay(500);

  String httpRequest = "GET " + url + " HTTP/1.1\r\n";
  httpRequest += "Host: " + String(GPS_SERVER_HOST) + "\r\n";
  httpRequest += "Connection: close\r\n\r\n";

  String sendCmd = "AT+CIPSEND=" + String(httpRequest.length());
  response = sendATCommand(sendCmd, 2000);

  if (response.indexOf(">") != -1) {
    gsmSerial.print(httpRequest);

    response = readGSMResponse(5000);

    if (response.indexOf("SEND OK") != -1 || response.indexOf("200") != -1) {
      Serial.println(" -> [OK] Sent.");
      sendATCommand("AT+CIPCLOSE", 2000);
      return true;
    }
  }

  Serial.println(" -> [ERROR] Failed.");
  sendATCommand("AT+CIPCLOSE", 2000);
  return false;
}

bool sendLocationToServer() {
  if (!gps.location.isValid()) {
    Serial.println("[WARN] GPS location not valid yet");
    return false;
  }

  if (!gprsConnected) {
    if (!initGPRS()) {
      Serial.println("[ERROR] Could not connect to GPRS");
      return false;
    }
  }

  double lat = gps.location.lat();
  double lon = gps.location.lng();
  double speed = gps.speed.kmph();
  double altitude = gps.altitude.meters();
  double hdop = gps.hdop.hdop();
  int sats = gps.satellites.value();

  String url = "/?id=" + String(GPS_DEVICE_ID);
  url += "&lat=" + String(lat, 6);
  url += "&lon=" + String(lon, 6);

  if (gps.date.isValid() && gps.date.year() > 2020) {
    unsigned long timestamp = getUnixTimestamp();
    url += "&timestamp=" + String(timestamp);
    Serial.println("Timestamp included: " + String(timestamp));
  } else {
    Serial.println("WARN: GPS Date invalid (" + String(gps.date.year()) + "), omitting timestamp so server uses current time.");
  }

  url += "&speed=" + String(speed, 2);
  url += "&bearing=0";
  url += "&altitude=" + String(altitude, 1);
  url += "&accuracy=" + String(hdop * 5, 1);
  url += "&batt=100";

  Serial.print("[AUTO] GPS: " + String(lat, 5) + "," + String(lon, 5) + " Sats:" + String(sats));

  bool anySuccess = false;
  for (int i = 0; i < NUM_SERVERS; i++) {
    Serial.println("\n[AUTO] Sending to Server " + String(i + 1) + ": " + String(GPS_SERVER_IPS[i]));
    if (sendHTTPRequest(url, GPS_SERVER_IPS[i])) {
      anySuccess = true;
    }
  }
  return anySuccess;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("       LOCATION TRACKER STARTING       ");
  Serial.println("========================================\n");

  Serial.println("[GPS] Initializing on pins RX:" + String(GPS_RX_PIN) + " TX:" + String(GPS_TX_PIN));
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[GPS] Serial initialized at " + String(GPS_BAUD) + " baud");

  Serial.println("\n[GSM] Initializing on pins RX:" + String(GSM_RX_PIN) + " TX:" + String(GSM_TX_PIN));
  gsmSerial.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  Serial.println("[GSM] Serial initialized at 9600 baud");

  delay(3000);
  Serial.println("\n[GSM] Testing connection...\n");

  initializeGSM();

  if (LIVE_TRACKING_ENABLED) {
    Serial.println("\n[GPRS] Initializing GPRS for live tracking...");
    if (initGPRS()) {
      Serial.println("[GPRS] Ready for live tracking!");
    } else {
      Serial.println("[GPRS] Will retry later...");
    }
  }

  Serial.println("\n========================================");
  Serial.println("       SYSTEM READY - WAITING SMS      ");
  if (LIVE_TRACKING_ENABLED) {
    Serial.println("       LIVE TRACKING: ENABLED          ");
    for (int i = 0; i < NUM_SERVERS; i++) {
      Serial.println("       Server " + String(i + 1) + ": " + String(GPS_SERVER_IPS[i]));
    }
    Serial.println("       Device ID: " + String(GPS_DEVICE_ID));
  }
  Serial.println("========================================\n");
}

void initializeGSM() {
  String response;

  Serial.println("--- Step 1: Testing AT Connection ---");
  response = sendATCommand("AT", 2000);
  if (response.indexOf("OK") != -1) {
    Serial.println("[OK] GSM Module Connected!\n");
  } else {
    Serial.println("[ERROR] GSM Module NOT Responding!\n");
  }
  delay(500);

  Serial.println("--- Step 2: Disable Echo ---");
  sendATCommand("ATE0", 1000);
  delay(500);

  Serial.println("--- Step 3: Set Character Set to GSM ---");
  response = sendATCommand("AT+CSCS=\"GSM\"", 2000);
  if (response.indexOf("OK") != -1) {
    Serial.println("[OK] Character Set: GSM\n");
  } else {
    Serial.println("[INFO] GSM charset not supported, will decode UCS2\n");
  }
  delay(500);

  Serial.println("--- Step 4: Check Signal Quality ---");
  response = sendATCommand("AT+CSQ", 2000);
  delay(500);

  Serial.println("--- Step 5: Check Network Registration ---");
  response = sendATCommand("AT+CREG?", 2000);
  if (response.indexOf("+CREG: 0,1") != -1 || response.indexOf("+CREG: 0,5") != -1) {
    Serial.println("[OK] Registered on Network!\n");
  } else {
    Serial.println("[WARNING] Not registered yet.\n");
  }
  delay(500);

  Serial.println("--- Step 6: Set SMS Text Mode ---");
  response = sendATCommand("AT+CMGF=1", 2000);
  if (response.indexOf("OK") != -1) {
    Serial.println("[OK] SMS Text Mode Enabled!\n");
  }
  delay(500);

  Serial.println("--- Step 7: Set SMS Notification ---");
  response = sendATCommand("AT+CNMI=1,2,0,0,0", 2000);
  if (response.indexOf("OK") != -1) {
    Serial.println("[OK] SMS Notification Enabled!\n");
  }
  delay(500);

  Serial.println("--- Step 8: Check SIM Card ---");
  sendATCommand("AT+CNUM", 2000);
  delay(500);

  while (gsmSerial.available()) {
    gsmSerial.read();
  }
}

void loop() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (LIVE_TRACKING_ENABLED) {
    unsigned long currentTime = millis();
    if (currentTime - lastSendTime >= SEND_INTERVAL_MS) {
      lastSendTime = currentTime;

      if (gps.location.isValid()) {
        Serial.println("\n[AUTO] Sending live location...");
        sendLocationToServer();
      } else {
        Serial.println("[AUTO] Waiting for GPS fix... Satellites: " + String(gps.satellites.value()));
      }
    }
  }

  if (gsmSerial.available()) {
    String data = gsmSerial.readString();
    data.trim();

    if (data.indexOf("+CMT:") != -1) {
      Serial.println("*** NEW SMS RECEIVED ***");

      int firstQuote = data.indexOf('"');
      int secondQuote = data.indexOf('"', firstQuote + 1);
      String senderNumber = data.substring(firstQuote + 1, secondQuote);

      if (isUCS2(senderNumber)) {
        Serial.println("Sender (UCS2 Raw): " + senderNumber);
        senderNumber = decodeUCS2(senderNumber);
        Serial.println("Sender (Decoded): " + senderNumber);
      } else {
        Serial.println("Sender: " + senderNumber);
      }

      int bodyStart = data.lastIndexOf('\n');
      String smsBody = "";
      if (bodyStart != -1) {
        smsBody = data.substring(bodyStart + 1);
        smsBody.trim();
      }

      Serial.println("Body (Raw): " + smsBody);

      String decodedBody = smsBody;
      if (isUCS2(smsBody)) {
        decodedBody = decodeUCS2(smsBody);
        Serial.println("Body (Decoded): " + decodedBody);
      }

      decodedBody.toLowerCase();

      if (decodedBody.indexOf("location") != -1) {
        Serial.println("\n*** LOCATION REQUEST DETECTED ***");
        sendResponse(senderNumber);
      } else {
        Serial.println("No 'location' keyword found in: " + decodedBody);
      }
    }
  }
}

void sendResponse(String number) {
  Serial.println("\n========================================");
  Serial.println("         SENDING SMS RESPONSE          ");
  Serial.println("========================================");
  Serial.println("To: " + number);

  Serial.println("To: " + number);

  String message = "";
  if (gps.location.isValid()) {
    message = "Car Location:\n";
    message += "Lat: " + String(gps.location.lat(), 6) + "\n";
    message += "Lon: " + String(gps.location.lng(), 6) + "\n";
    message += "Speed: " + String(gps.speed.kmph(), 1) + " km/h\n";
    message += "Map: https://maps.google.com/?q=" + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
  } else {
    message = "GPS is looking for satellites...\n";
    if (gps.date.isValid()) {
       message += "Last seen: " + String(gps.time.hour()) + ":" + String(gps.time.minute());
    }
  }

  while (gsmSerial.available()) {
    gsmSerial.read();
  }

  sendATCommand("AT+CIPCLOSE", 1000);
  delay(500);

  Serial.println("\nStep 1: Sending AT+CMGS command...");
  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(number);
  gsmSerial.println("\"");

  Serial.println("Step 2: Waiting for '>' prompt...");
  unsigned long startTime = millis();
  bool promptReceived = false;
  String promptResponse = "";

  while (millis() - startTime < 10000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      promptResponse += c;
      Serial.print(c);
      if (c == '>') {
        promptReceived = true;
        Serial.println("\n[OK] Prompt received!");
        break;
      }
    }
    delay(10);
  }

  if (!promptReceived) {
    Serial.println("\n[ERROR] No '>' prompt! Response was: " + promptResponse);
    Serial.println("Cancelling SMS...");
    gsmSerial.write(27);
    delay(1000);
    while (gsmSerial.available()) gsmSerial.read();
    return;
  }

  Serial.println("Step 3: Sending message: " + message);
  delay(200);
  gsmSerial.print(message);
  delay(200);

  Serial.println("Step 4: Sending Ctrl+Z (0x1A)...");
  gsmSerial.write(0x1A);

  Serial.println("Step 5: Waiting for send confirmation (up to 30 sec)...");
  String response = "";
  startTime = millis();

  while (millis() - startTime < 30000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
      Serial.print(c);

      if (response.indexOf("+CMGS:") != -1 && response.indexOf("OK") != -1) {
        Serial.println("\n\n*** SMS SENT SUCCESSFULLY! ***\n");
        return;
      }
      if (response.indexOf("ERROR") != -1) {
        Serial.println("\n\n*** SMS SEND FAILED! ***\n");
        return;
      }
    }
    delay(10);
  }

  Serial.println("\n[TIMEOUT] No confirmation received.");
  Serial.println("Last response: " + response);
}
