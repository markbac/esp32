# ESP32-S3 Weather Display

Home weather station firmware for the **Adafruit Feather ESP32-S3 Reverse TFT** (240 × 135 colour display).

**Pages:** current conditions · hourly · 5-day forecast · animated rain radar with a 2-hour "rain soon" forecast · air quality, UV and pollen · Met Office warnings + AuroraWatch UK aurora alerts · sunrise, sunset and moon phase · nearby flights · system status.

**Also:** quarter-hourly history saved to flash (8 days), and a local web dashboard at `http://weather.local` that mirrors every screen and charts the last 7 days (CSV download included). The on-board LED glows for Met Office amber/red warnings and pulses green for aurora alerts.

All data sources are free and need no API keys: Open-Meteo, Met Office, AuroraWatch UK, RainViewer, OpenStreetMap, adsb.lol, adsbdb.

## Build

1. Install [PlatformIO](https://platformio.org/) (`pip install platformio`).
2. Copy `include/secrets.example.h` to `include/secrets.h` and enter your Wi-Fi details.
3. Set your location, Met Office region and county in `include/config.h`.
4. Build and flash:

```bash
pio run -t upload
```

On Windows, `scripts/flash.ps1` handles the board switching to a new COM port partway through an upload.

## Buttons

| Button | Action |
|---|---|
| D0 | Next page |
| D1 | Previous page |
| D2 | Refresh all data now |

## Documentation

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture (C4 diagrams), data flow and design decisions. Run `doxygen Doxyfile` for API docs.
