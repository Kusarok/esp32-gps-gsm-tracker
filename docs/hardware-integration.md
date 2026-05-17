# Hardware Integration Notes

## Pin Mapping

| ESP32 GPIO | Peripheral Signal | Direction | Notes |
| --- | --- | --- | --- |
| GPIO32 | GPS TX → ESP32 RX2 | Input | Receives NMEA data from the GNSS module. |
| GPIO33 | GPS RX ← ESP32 TX2 | Output | Optional command/configuration channel for the GNSS module. |
| GPIO26 | GSM TX → ESP32 RX1 | Input | Receives AT responses and SMS notifications from the modem. |
| GPIO27 | GSM RX ← ESP32 TX1 | Output | Sends AT commands and SMS payloads to the modem. |
| GND | Common ground | Reference | ESP32, GPS, GSM, and power supply grounds must be tied together. |

## Power Design

- Use a dedicated supply sized for cellular transmit bursts.
- Keep modem power leads short and low impedance.
- Add bulk capacitance close to the GSM modem power pins.
- Place the GPS antenna away from switching regulators and the GSM antenna.
- Vehicle installations should include fuse protection, transient suppression, reverse-polarity protection, and an automotive-rated regulator.
