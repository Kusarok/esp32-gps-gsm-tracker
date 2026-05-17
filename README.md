# ESP32 GPS GSM Tracker

A production-ready Arduino sketch for building a compact real-time vehicle or asset tracker with an ESP32, a GPS/GNSS receiver, and a GSM/GPRS modem. The firmware reads NMEA location data, sends periodic HTTP telemetry over GPRS, and can reply to SMS messages containing the keyword `location` with a Google Maps link.

## Table of Contents

- [Features](#features)
- [Project Status](#project-status)
- [Hardware Modules Required](#hardware-modules-required)
- [Software Requirements](#software-requirements)
- [Repository Structure](#repository-structure)
- [Wiring](#wiring)
- [Configuration](#configuration)
- [Build and Upload](#build-and-upload)
- [Server Integration](#server-integration)
- [SMS Commands](#sms-commands)
- [Power Design Notes](#power-design-notes)
- [Troubleshooting](#troubleshooting)
- [Useful Websites and References](#useful-websites-and-references)
- [Security Notes](#security-notes)
- [License](#license)

## Features

- ESP32-based tracker firmware for Arduino IDE or Arduino CLI.
- GPS/GNSS parsing with TinyGPS++.
- GSM/GPRS data connection through AT commands.
- Periodic live tracking over HTTP GET requests.
- Multiple server IP fallback support.
- SMS location request support.
- Google Maps link generation for SMS replies.
- GPS timestamp, speed, altitude, satellite count, and HDOP-based accuracy support.
- Public-safe default configuration placeholders for device ID, APN, server host, and server IPs.

## Project Status

This project is intended for hobby, prototyping, fleet proof-of-concept, and educational tracking systems. Before using it in a production or safety-critical environment, validate the power supply, enclosure, cellular coverage, antenna placement, data privacy requirements, and backend availability.

## Hardware Modules Required

| Category | Recommended Module | Purpose | Notes |
| --- | --- | --- | --- |
| Microcontroller | ESP32 DevKit / ESP32-WROOM development board | Main controller | Requires at least two hardware UARTs for reliable GPS and GSM communication. |
| GPS/GNSS receiver | u-blox NEO-6M, NEO-M8N, ATGM336H, or compatible NMEA receiver | Position, speed, date, and time | Use a module with an external antenna connector when installing inside a vehicle. |
| GSM/GPRS modem | SIM800L, SIM800C, SIM900, or compatible 2G AT-command modem | SMS and GPRS telemetry | SIM800/SIM900 are 2G modules; confirm 2G service is still available in your country. |
| SIM card | Data/SMS-enabled SIM | Cellular connectivity | Disable PIN lock before deployment and confirm the APN with the carrier. |
| GSM antenna | 2G quad-band antenna | Cellular signal | Keep away from the GPS antenna and noisy power electronics. |
| GPS antenna | Active or passive GPS antenna, depending on module | Satellite reception | Place with clear sky view when possible. |
| Power supply | Stable 5 V source for ESP32 plus 4.0 V capable high-current supply for SIM800-class modem | System power | GSM modems can draw current bursts near 2 A. Do not power SIM800L directly from the ESP32 3.3 V pin. |
| Buck converter | LM2596, MP1584, or automotive-grade DC-DC converter | Vehicle battery conversion | Use adequate filtering and transient protection for vehicle installs. |
| Logic-level wiring | Jumper wires or PCB traces | UART connections | Most ESP32 GPIOs are 3.3 V logic. Check your modem breakout logic levels. |
| Optional backup battery | Li-ion/LiPo plus charger/protection board | Operation during power loss | Size according to modem current peaks and expected runtime. |
| Optional enclosure | Plastic or weather-resistant enclosure | Mechanical protection | Avoid metal enclosures around antennas unless external antennas are used. |

## Software Requirements

- [Arduino IDE](https://www.arduino.cc/en/software) or [Arduino CLI](https://arduino.github.io/arduino-cli/).
- [Arduino ESP32 Core by Espressif](https://docs.espressif.com/projects/arduino-esp32/en/latest/).
- [TinyGPS++ library](https://github.com/mikalhart/TinyGPSPlus).
- USB serial driver for your ESP32 board, such as CP210x or CH340, depending on the board.

## Repository Structure

```text
.
├── esp32_gps_gsm_tracker.ino   # Main Arduino sketch
├── README.md                   # Project documentation
├── LICENSE                     # Project license
└── .gitignore                  # Git ignore rules
```

The sketch was renamed from a generic `tracker.ino` filename to `esp32_gps_gsm_tracker.ino` so the project name is clearer and more professional.

## Wiring

Default UART pin assignments are defined in the sketch:

| ESP32 Pin | Connects To | Description |
| --- | --- | --- |
| GPIO32 | GPS TX | ESP32 receives NMEA data from the GPS module. |
| GPIO33 | GPS RX | ESP32 transmits to GPS module, if supported. |
| GPIO26 | GSM TX | ESP32 receives modem responses. |
| GPIO27 | GSM RX | ESP32 sends AT commands to the modem. |
| GND | GPS GND and GSM GND | Common ground is required. |
| 5 V / VIN | ESP32 power input | Depends on board design. |
| 3.7 V to 4.2 V high-current supply | SIM800-class modem VCC | Use a supply that can handle cellular transmit bursts. |

If your hardware uses different pins, update these constants in `esp32_gps_gsm_tracker.ino`:

```cpp
#define GPS_RX_PIN 32
#define GPS_TX_PIN 33
#define GSM_RX_PIN 26
#define GSM_TX_PIN 27
```

## Configuration

Before uploading, edit the configuration block at the top of `esp32_gps_gsm_tracker.ino`.

```cpp
#define GPS_SERVER_HOST "SERVER_HOST"
const char* GPS_SERVER_IPS[] = {"SERVER_IP_1", "SERVER_IP_2"};
const int NUM_SERVERS = 2;
#define GPS_SERVER_PORT 22945
#define GPS_DEVICE_ID "DEVICE_ID"

#define APN "YOUR_APN"
#define APN_USER "YOUR_APN_USER"
#define APN_PASS "YOUR_APN_PASSWORD"
```

Replace the placeholders as follows:

| Placeholder | Meaning | Example Format |
| --- | --- | --- |
| `SERVER_HOST` | HTTP `Host` header expected by your telemetry platform | `example.tracking.server` |
| `SERVER_IP_1`, `SERVER_IP_2` | Backend IP addresses for TCP connection attempts | `203.0.113.10` |
| `GPS_SERVER_PORT` | TCP port exposed by your tracking backend | `80`, `5055`, or provider-specific port |
| `DEVICE_ID` | Public-safe tracker identifier registered on your backend | `VEHICLE_001` |
| `YOUR_APN` | Carrier APN | Carrier-specific |
| `YOUR_APN_USER` | APN username, if required | Usually empty or carrier-specific |
| `YOUR_APN_PASSWORD` | APN password, if required | Usually empty or carrier-specific |

Never commit real APNs, private server hosts, production IP addresses, SIM credentials, or device identifiers to a public repository.

## Build and Upload

### Arduino IDE

1. Install Arduino IDE.
2. Add the ESP32 board package through Boards Manager.
3. Install the TinyGPS++ library through Library Manager.
4. Open `esp32_gps_gsm_tracker.ino`.
5. Select your ESP32 board and serial port.
6. Configure the placeholders described above.
7. Upload the sketch.
8. Open Serial Monitor at `115200` baud.

### Arduino CLI Example

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install TinyGPSPlus
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 .
```

Your fully qualified board name may be different depending on the ESP32 board package and board model.

## Server Integration

The firmware sends HTTP GET requests in this format:

```text
/?id=DEVICE_ID&lat=LATITUDE&lon=LONGITUDE&timestamp=UNIX_TIME&speed=SPEED&bearing=0&altitude=ALTITUDE&accuracy=ACCURACY&batt=100
```

The backend must accept TCP connections on `GPS_SERVER_PORT` and process the request path. The firmware currently sends a simple HTTP/1.1 request with a configurable `Host` header.

Compatible backend options include:

- A custom HTTP endpoint.
- [Traccar](https://www.traccar.org/) with a compatible HTTP/protocol endpoint.
- [flespi](https://flespi.com/) or another IoT gateway configured to accept this payload shape.

Confirm the exact path, port, and authentication requirements for your backend before deployment.

## SMS Commands

Send an SMS containing the word:

```text
location
```

The device replies with:

- Latitude.
- Longitude.
- Speed in km/h.
- Google Maps URL.

If the GPS fix is not available, the device replies that it is still searching for satellites.

## Power Design Notes

- SIM800-class modems can reset or fail to attach to the network if the power supply cannot handle transmit bursts.
- Use short, thick power wires for the modem.
- Add bulk capacitance near the modem power pins when using breadboards or long leads.
- Keep GPS antenna wiring away from the GSM antenna and switching regulators.
- In vehicles, use a protected automotive DC-DC converter and consider fuse, TVS diode, reverse-polarity protection, and ignition sensing.

## Troubleshooting

| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| `GSM Module NOT Responding` | Wrong UART pins, missing common ground, wrong baud rate, or no modem power | Check wiring, power, and `GSM_RX_PIN` / `GSM_TX_PIN`. |
| GPRS fails at APN setup | Wrong APN credentials or SIM not ready | Verify APN with carrier, disable SIM PIN, test SIM in a phone. |
| TCP connection fails | Wrong server IP, blocked port, no data plan, or weak signal | Verify IP/port, test with another client, check signal quality. |
| GPS location not valid | No sky view, wrong UART pins, GPS baud mismatch, cold start | Move antenna outdoors and wait several minutes. |
| SMS not received | SIM plan issue, text mode not enabled, network not registered | Check `AT+CREG?`, signal, and SMS capability. |
| Device reboots when GSM transmits | Power supply voltage drop | Use a dedicated high-current modem supply. |

## Useful Websites and References

- Arduino IDE: <https://www.arduino.cc/en/software>
- Arduino CLI: <https://arduino.github.io/arduino-cli/>
- Arduino ESP32 Core documentation: <https://docs.espressif.com/projects/arduino-esp32/en/latest/>
- Arduino ESP32 Core GitHub repository: <https://github.com/espressif/arduino-esp32>
- TinyGPS++ GitHub repository: <https://github.com/mikalhart/TinyGPSPlus>
- TinyGPS++ Arduino Library page: <https://www.arduinolibraries.info/libraries/tiny-gps-plus>
- Espressif ESP32 product page: <https://www.espressif.com/en/products/socs/esp32>
- SIMCom product resources: <https://www.simcom.com/product/>
- u-blox GNSS products: <https://www.u-blox.com/en/positioning-chips-and-modules>
- Traccar GPS tracking platform: <https://www.traccar.org/>
- flespi IoT and telematics platform: <https://flespi.com/>
- Google Maps search URL format: <https://maps.google.com/?q=LAT,LON>

## Security Notes

- Do not publish real device IDs, APN credentials, server hosts, backend IP addresses, tokens, or SIM-related credentials.
- Use placeholder values in source control and keep production values in private deployment notes.
- Consider server-side authentication before accepting location data.
- Treat GPS data as sensitive personal or operational information.
- If the device is installed in a vehicle, comply with local tracking, consent, privacy, and telecom regulations.

## License

This project is released under the license included in [`LICENSE`](LICENSE).
