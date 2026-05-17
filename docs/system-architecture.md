# System Architecture

The ESP32 GPS/GSM tracker is organized as a compact IoT telemetry device that separates sensing, transport, command, and backend integration responsibilities.

## Functional Blocks

| Block | Responsibility |
| --- | --- |
| ESP32 application firmware | Coordinates UART peripherals, GPS parsing, SMS command handling, and telemetry publishing. |
| GNSS receiver | Streams NMEA sentences that provide latitude, longitude, speed, altitude, satellite count, HDOP, date, and UTC time. |
| GSM/GPRS modem | Provides SMS control-plane messaging and TCP/IP data-plane connectivity over a cellular network. |
| Telemetry backend | Receives HTTP GET telemetry frames and applies storage, visualization, alerting, or fleet tracking workflows. |

## Runtime Flow

1. GPS UART is serviced continuously in the main loop.
2. TinyGPS++ validates and decodes NMEA data into the current fix snapshot.
3. At a fixed interval, the firmware builds an HTTP telemetry payload.
4. The GSM/GPRS transport layer opens a TCP socket to each configured endpoint.
5. The backend receives position, timing, speed, altitude, accuracy, and battery metadata.
6. Incoming SMS messages are parsed for the `location` command and answered with the latest fix.

## Engineering Notes

- Production deployments should externalize APN credentials, device identifiers, and backend addresses outside of public source control.
- SIM800-class modems require a power supply capable of handling high transient current during RF transmit bursts.
- GPS receivers need good sky visibility and careful antenna placement to maintain fix quality.
