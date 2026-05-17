#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// -----------------------------------------------------------------------------
// ESP32 GPS + GSM/GPRS tracker firmware
// -----------------------------------------------------------------------------
// This sketch implements a small embedded telemetry pipeline:
//   1. Continuously ingest NMEA sentences from the GPS receiver.
//   2. Convert the latest fix into a telemetry HTTP query string.
//   3. Transport the payload over a SIM800-class GSM/GPRS TCP connection.
//   4. Support an SMS-based "location" command for out-of-band diagnostics.
//
// Keep production APNs, server addresses, credentials, and unique device IDs out
// of source control. Replace the placeholder values below only in private builds.
// -----------------------------------------------------------------------------

namespace Pins {
constexpr uint8_t GPS_RX = 32;  // ESP32 RX pin connected to GPS TX
constexpr uint8_t GPS_TX = 33;  // ESP32 TX pin connected to GPS RX
constexpr uint8_t GSM_RX = 26;  // ESP32 RX pin connected to GSM TX
constexpr uint8_t GSM_TX = 27;  // ESP32 TX pin connected to GSM RX
}  // namespace Pins

namespace SerialConfig {
constexpr uint32_t DEBUG_BAUD = 115200;
constexpr uint32_t GPS_BAUD = 115200;
constexpr uint32_t GSM_BAUD = 9600;
}  // namespace SerialConfig

namespace TelemetryConfig {
constexpr const char* SERVER_HOST = "tracking.example.invalid";
constexpr const char* SERVER_IPS[] = {"192.0.2.10", "192.0.2.11"};
constexpr uint8_t SERVER_COUNT = sizeof(SERVER_IPS) / sizeof(SERVER_IPS[0]);
constexpr uint16_t SERVER_PORT = 22945;
constexpr const char* DEVICE_ID = "DEVICE_ID_PLACEHOLDER";
constexpr unsigned long SEND_INTERVAL_MS = 15000UL;
constexpr bool LIVE_TRACKING_ENABLED = true;
}  // namespace TelemetryConfig

namespace CellularConfig {
constexpr const char* APN = "APN_PLACEHOLDER";
constexpr const char* APN_USER = "APN_USER_PLACEHOLDER";
constexpr const char* APN_PASS = "APN_PASSWORD_PLACEHOLDER";
}  // namespace CellularConfig

namespace TimeConfig {
constexpr unsigned long FALLBACK_UNIX_EPOCH = 1734307200UL;
constexpr uint16_t MIN_VALID_GPS_YEAR = 2021;
}  // namespace TimeConfig

struct GpsTelemetry {
  double latitude;
  double longitude;
  double speedKmph;
  double altitudeMeters;
  double hdop;
  uint32_t satellites;
};

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial gsmSerial(1);

unsigned long lastTelemetrySendMs = 0;
bool gprsConnected = false;

String decodeUCS2(const String& hexString);
bool isUCS2(const String& value);
String readGSMResponse(unsigned long timeoutMs = 2000UL);
String sendATCommand(const String& command, unsigned long timeoutMs = 2000UL);
void clearGSMBuffer();
bool initializeGPRS();
bool sendHTTPRequest(const String& url, const String& serverIp);
bool sendLocationToServer();
unsigned long getUnixTimestamp();
String buildTelemetryUrl(const GpsTelemetry& telemetry);
GpsTelemetry readTelemetrySnapshot();
void initializeGSM();
void processIncomingSMS(const String& rawMessage);
String extractSmsSender(const String& rawMessage);
String extractSmsBody(const String& rawMessage);
void sendLocationSms(const String& number);
void serviceGPS();
void serviceLiveTelemetry();
void serviceSMS();

String decodeUCS2(const String& hexString) {
  String normalized = hexString;
  String result;
  normalized.trim();

  for (int i = 0; i + 4 <= normalized.length(); i += 4) {
    const String hexChar = normalized.substring(i, i + 4);
    const long charCode = strtol(hexChar.c_str(), nullptr, 16);

    if (charCode >= 32 && charCode <= 126) {
      result += static_cast<char>(charCode);
    }
  }

  return result;
}

bool isUCS2(const String& value) {
  if (value.length() < 4 || value.length() % 4 != 0) {
    return false;
  }

  for (int i = 0; i < value.length(); i++) {
    if (!isHexadecimalDigit(value.charAt(i))) {
      return false;
    }
  }

  return true;
}

void clearGSMBuffer() {
  while (gsmSerial.available()) {
    gsmSerial.read();
  }
}

String readGSMResponse(unsigned long timeoutMs) {
  String response;
  const unsigned long startTime = millis();

  while (millis() - startTime < timeoutMs) {
    while (gsmSerial.available()) {
      response += static_cast<char>(gsmSerial.read());
    }
    delay(10);
  }

  return response;
}

String sendATCommand(const String& command, unsigned long timeoutMs) {
  clearGSMBuffer();
  gsmSerial.println(command);
  return readGSMResponse(timeoutMs);
}

unsigned long getUnixTimestamp() {
  if (!gps.date.isValid() || !gps.time.isValid()) {
    return TimeConfig::FALLBACK_UNIX_EPOCH + (millis() / 1000UL);
  }

  const int year = gps.date.year();
  const int month = gps.date.month();
  const int day = gps.date.day();
  const int hour = gps.time.hour();
  const int minute = gps.time.minute();
  const int second = gps.time.second();

  unsigned long days = 0;
  for (int y = 1970; y < year; y++) {
    days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
  }

  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    daysInMonth[1] = 29;
  }

  for (int m = 1; m < month; m++) {
    days += daysInMonth[m - 1];
  }

  days += day - 1;
  return (days * 86400UL) + (hour * 3600UL) + (minute * 60UL) + second;
}

bool initializeGPRS() {
  Serial.println(F("\n--- Initializing GSM/GPRS transport layer ---"));

  sendATCommand("AT+CIPSHUT", 3000UL);
  delay(1000);

  String response = sendATCommand("AT+CIPMUX=0", 2000UL);
  if (response.indexOf("OK") == -1) {
    Serial.println(F("[ERROR] Failed to configure single TCP connection mode"));
    return false;
  }

  const String apnCommand = "AT+CSTT=\"" + String(CellularConfig::APN) + "\",\"" +
                            String(CellularConfig::APN_USER) + "\",\"" +
                            String(CellularConfig::APN_PASS) + "\"";
  response = sendATCommand(apnCommand, 3000UL);
  if (response.indexOf("OK") == -1) {
    Serial.println(F("[ERROR] APN setup failed"));
    return false;
  }

  Serial.println(F("[GPRS] Bringing up wireless data session..."));
  response = sendATCommand("AT+CIICR", 10000UL);
  if (response.indexOf("OK") == -1 && response.indexOf("ERROR") != -1) {
    Serial.println(F("[ERROR] PDP context activation failed; verify SIM data plan and APN"));
    return false;
  }

  response = sendATCommand("AT+CIFSR", 3000UL);
  if (response.indexOf("ERROR") != -1 || response.length() < 7) {
    Serial.println(F("[ERROR] Modem did not return an IP address"));
    return false;
  }

  gprsConnected = true;
  Serial.println("[OK] GPRS attached with modem IP: " + response);
  return true;
}

bool sendHTTPRequest(const String& url, const String& serverIp) {
  const String connectCommand = "AT+CIPSTART=\"TCP\",\"" + serverIp + "\",\"" +
                                String(TelemetryConfig::SERVER_PORT) + "\"";
  String response = sendATCommand(connectCommand, 10000UL);

  if (response.indexOf("CONNECT OK") == -1 && response.indexOf("ALREADY CONNECT") == -1) {
    Serial.println("[ERROR] TCP connection failed for " + serverIp);
    sendATCommand("AT+CIPCLOSE", 2000UL);
    gprsConnected = false;
    return false;
  }

  const String httpRequest = "GET " + url + " HTTP/1.1\r\n" +
                             "Host: " + String(TelemetryConfig::SERVER_HOST) + "\r\n" +
                             "Connection: close\r\n\r\n";
  response = sendATCommand("AT+CIPSEND=" + String(httpRequest.length()), 2000UL);

  if (response.indexOf(">") != -1) {
    gsmSerial.print(httpRequest);
    response = readGSMResponse(5000UL);

    if (response.indexOf("SEND OK") != -1 || response.indexOf("200") != -1) {
      Serial.println(F("[OK] Telemetry payload accepted by modem"));
      sendATCommand("AT+CIPCLOSE", 2000UL);
      return true;
    }
  }

  Serial.println(F("[ERROR] Telemetry send failed"));
  sendATCommand("AT+CIPCLOSE", 2000UL);
  return false;
}

GpsTelemetry readTelemetrySnapshot() {
  return {
      gps.location.lat(),
      gps.location.lng(),
      gps.speed.kmph(),
      gps.altitude.meters(),
      gps.hdop.hdop(),
      gps.satellites.value(),
  };
}

String buildTelemetryUrl(const GpsTelemetry& telemetry) {
  String url = "/?id=" + String(TelemetryConfig::DEVICE_ID);
  url += "&lat=" + String(telemetry.latitude, 6);
  url += "&lon=" + String(telemetry.longitude, 6);

  if (gps.date.isValid() && gps.date.year() >= TimeConfig::MIN_VALID_GPS_YEAR) {
    url += "&timestamp=" + String(getUnixTimestamp());
  } else {
    Serial.println(F("[WARN] GPS date is not valid; backend should apply receive timestamp"));
  }

  url += "&speed=" + String(telemetry.speedKmph, 2);
  url += "&bearing=0";
  url += "&altitude=" + String(telemetry.altitudeMeters, 1);
  url += "&accuracy=" + String(telemetry.hdop * 5, 1);
  url += "&batt=100";

  return url;
}

bool sendLocationToServer() {
  if (!gps.location.isValid()) {
    Serial.println(F("[WARN] GPS location is not valid yet"));
    return false;
  }

  if (!gprsConnected && !initializeGPRS()) {
    Serial.println(F("[ERROR] Could not establish GPRS transport"));
    return false;
  }

  const GpsTelemetry telemetry = readTelemetrySnapshot();
  const String url = buildTelemetryUrl(telemetry);

  Serial.print("[GPS] Fix: ");
  Serial.print(telemetry.latitude, 5);
  Serial.print(",");
  Serial.print(telemetry.longitude, 5);
  Serial.println(" sats=" + String(telemetry.satellites));

  bool anySuccess = false;
  for (uint8_t i = 0; i < TelemetryConfig::SERVER_COUNT; i++) {
    Serial.println("[GPRS] Sending telemetry to endpoint " + String(i + 1) + ": " +
                   String(TelemetryConfig::SERVER_IPS[i]));
    anySuccess = sendHTTPRequest(url, TelemetryConfig::SERVER_IPS[i]) || anySuccess;
  }

  return anySuccess;
}

void initializeGSM() {
  Serial.println(F("--- GSM modem initialization sequence ---"));

  String response = sendATCommand("AT", 2000UL);
  Serial.println(response.indexOf("OK") != -1 ? F("[OK] GSM modem responded")
                                              : F("[ERROR] GSM modem did not respond"));

  sendATCommand("ATE0", 1000UL);

  response = sendATCommand("AT+CSCS=\"GSM\"", 2000UL);
  Serial.println(response.indexOf("OK") != -1 ? F("[OK] GSM character set selected")
                                              : F("[INFO] GSM charset unavailable; UCS2 decode enabled"));

  response = sendATCommand("AT+CSQ", 2000UL);
  Serial.println("[GSM] Signal quality response: " + response);

  response = sendATCommand("AT+CREG?", 2000UL);
  if (response.indexOf("+CREG: 0,1") != -1 || response.indexOf("+CREG: 0,5") != -1) {
    Serial.println(F("[OK] Registered on cellular network"));
  } else {
    Serial.println(F("[WARN] Modem is not registered yet"));
  }

  response = sendATCommand("AT+CMGF=1", 2000UL);
  if (response.indexOf("OK") != -1) {
    Serial.println(F("[OK] SMS text mode enabled"));
  }

  response = sendATCommand("AT+CNMI=1,2,0,0,0", 2000UL);
  if (response.indexOf("OK") != -1) {
    Serial.println(F("[OK] Live SMS notification enabled"));
  }

  sendATCommand("AT+CNUM", 2000UL);
  clearGSMBuffer();
}

String extractSmsSender(const String& rawMessage) {
  const int firstQuote = rawMessage.indexOf('"');
  const int secondQuote = rawMessage.indexOf('"', firstQuote + 1);

  if (firstQuote == -1 || secondQuote == -1) {
    return "";
  }

  String sender = rawMessage.substring(firstQuote + 1, secondQuote);
  sender.trim();
  return isUCS2(sender) ? decodeUCS2(sender) : sender;
}

String extractSmsBody(const String& rawMessage) {
  const int bodyStart = rawMessage.lastIndexOf('\n');
  String body = bodyStart == -1 ? rawMessage : rawMessage.substring(bodyStart + 1);
  body.trim();
  return isUCS2(body) ? decodeUCS2(body) : body;
}

void processIncomingSMS(const String& rawMessage) {
  if (rawMessage.indexOf("+CMT:") == -1) {
    return;
  }

  Serial.println(F("*** NEW SMS RECEIVED ***"));

  const String sender = extractSmsSender(rawMessage);
  String body = extractSmsBody(rawMessage);

  Serial.println("Sender: " + sender);
  Serial.println("Body: " + body);

  body.toLowerCase();
  if (body.indexOf("location") != -1) {
    Serial.println(F("[SMS] Location request accepted"));
    sendLocationSms(sender);
  } else {
    Serial.println(F("[SMS] Ignoring message without supported command keyword"));
  }
}

void sendLocationSms(const String& number) {
  Serial.println(F("\n--- Sending SMS location response ---"));
  Serial.println("To: " + number);

  String message;
  if (gps.location.isValid()) {
    message = "Car Location:\n";
    message += "Lat: " + String(gps.location.lat(), 6) + "\n";
    message += "Lon: " + String(gps.location.lng(), 6) + "\n";
    message += "Speed: " + String(gps.speed.kmph(), 1) + " km/h\n";
    message += "Map: https://maps.google.com/?q=" + String(gps.location.lat(), 6) + "," +
               String(gps.location.lng(), 6);
  } else {
    message = "GPS is looking for satellites...\n";
    if (gps.time.isValid()) {
      message += "Last GPS time: " + String(gps.time.hour()) + ":" + String(gps.time.minute());
    }
  }

  clearGSMBuffer();
  sendATCommand("AT+CIPCLOSE", 1000UL);
  delay(500);

  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(number);
  gsmSerial.println("\"");

  const unsigned long startTime = millis();
  bool promptReceived = false;
  String promptResponse;

  while (millis() - startTime < 10000UL) {
    if (gsmSerial.available()) {
      const char c = gsmSerial.read();
      promptResponse += c;
      Serial.print(c);
      if (c == '>') {
        promptReceived = true;
        break;
      }
    }
    delay(10);
  }

  if (!promptReceived) {
    Serial.println("\n[ERROR] SMS prompt timeout. Modem response: " + promptResponse);
    gsmSerial.write(27);
    delay(1000);
    clearGSMBuffer();
    return;
  }

  gsmSerial.print(message);
  delay(200);
  gsmSerial.write(0x1A);

  String response;
  const unsigned long confirmationStart = millis();
  while (millis() - confirmationStart < 30000UL) {
    if (gsmSerial.available()) {
      const char c = gsmSerial.read();
      response += c;
      Serial.print(c);

      if (response.indexOf("+CMGS:") != -1 && response.indexOf("OK") != -1) {
        Serial.println(F("\n[OK] SMS sent successfully"));
        return;
      }
      if (response.indexOf("ERROR") != -1) {
        Serial.println(F("\n[ERROR] SMS send failed"));
        return;
      }
    }
    delay(10);
  }

  Serial.println("\n[TIMEOUT] No SMS confirmation received. Last response: " + response);
}

void serviceGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
}

void serviceLiveTelemetry() {
  if (!TelemetryConfig::LIVE_TRACKING_ENABLED) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastTelemetrySendMs < TelemetryConfig::SEND_INTERVAL_MS) {
    return;
  }

  lastTelemetrySendMs = now;
  if (gps.location.isValid()) {
    Serial.println(F("\n[AUTO] Publishing live GPS telemetry"));
    sendLocationToServer();
  } else {
    Serial.println("[AUTO] Waiting for GPS fix; satellites=" + String(gps.satellites.value()));
  }
}

void serviceSMS() {
  if (!gsmSerial.available()) {
    return;
  }

  String data = gsmSerial.readString();
  data.trim();
  processIncomingSMS(data);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(SerialConfig::DEBUG_BAUD);
  delay(1000);

  Serial.println(F("\n========================================"));
  Serial.println(F("   ESP32 GPS/GSM TELEMETRY STARTING"));
  Serial.println(F("========================================\n"));

  Serial.println("[GPS] UART2 RX=" + String(Pins::GPS_RX) + " TX=" + String(Pins::GPS_TX));
  gpsSerial.begin(SerialConfig::GPS_BAUD, SERIAL_8N1, Pins::GPS_RX, Pins::GPS_TX);

  Serial.println("[GSM] UART1 RX=" + String(Pins::GSM_RX) + " TX=" + String(Pins::GSM_TX));
  gsmSerial.begin(SerialConfig::GSM_BAUD, SERIAL_8N1, Pins::GSM_RX, Pins::GSM_TX);

  delay(3000);
  initializeGSM();

  if (TelemetryConfig::LIVE_TRACKING_ENABLED) {
    Serial.println(F("\n[GPRS] Preparing live tracking data session"));
    if (!initializeGPRS()) {
      Serial.println(F("[GPRS] Initial attach failed; firmware will retry later"));
    }
  }

  Serial.println(F("\n========================================"));
  Serial.println(F("       SYSTEM READY"));
  Serial.println("       Live tracking: " + String(TelemetryConfig::LIVE_TRACKING_ENABLED ? "enabled" : "disabled"));
  Serial.println("       Device ID: " + String(TelemetryConfig::DEVICE_ID));
  for (uint8_t i = 0; i < TelemetryConfig::SERVER_COUNT; i++) {
    Serial.println("       Endpoint " + String(i + 1) + ": " + String(TelemetryConfig::SERVER_IPS[i]));
  }
  Serial.println(F("========================================\n"));
}

void loop() {
  serviceGPS();
  serviceLiveTelemetry();
  serviceSMS();
}
