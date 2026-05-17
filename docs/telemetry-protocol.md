# Telemetry Protocol

The firmware publishes GPS telemetry through a simple HTTP GET request over a GSM/GPRS TCP socket.

```text
/?id=DEVICE_ID_PLACEHOLDER&lat=LATITUDE&lon=LONGITUDE&timestamp=UNIX_TIME&speed=SPEED&bearing=0&altitude=ALTITUDE&accuracy=ACCURACY&batt=100
```

## Field Reference

| Field | Description |
| --- | --- |
| `id` | Sanitized public-safe device identifier registered by the tracking backend. |
| `lat` / `lon` | Real-time GPS acquisition output in decimal degrees. |
| `timestamp` | Unix timestamp derived from valid GPS date/time, omitted when GPS time is not trusted. |
| `speed` | Ground speed in km/h. |
| `bearing` | Reserved bearing field. Current firmware sends `0`. |
| `altitude` | GPS altitude in meters. |
| `accuracy` | HDOP-derived coarse accuracy estimate. |
| `batt` | Placeholder battery percentage for future power telemetry. |

The transport layer attempts each configured backend IP address, enabling a basic fault-tolerant networking pattern for constrained cellular links.
