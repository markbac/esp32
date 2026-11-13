# ESP32 projects

Firmware for ESP32 boards around the house. Each project lives in its own folder with its own README, build instructions and documentation.

| Project | Boards | What it is |
|---|---|---|
| [weather-display](weather-display/) | Elecrow CrowPanel 7" Basic, Adafruit Feather ESP32-S3 Reverse TFT | Home weather station and dashboard, plus a Wi-Fi diagnostics tool |

## weather-display

A weather station that reads a **WeatherFlow Tempest** over the local network and combines it with free online data. The two boards share one core:

- **CrowPanel 7"** (800 x 480 touch): 17 touch pages in 7 groups: live conditions, forecast, animated rain radar, alerts, air quality, electricity grid carbon, sun and moon, flights overhead (scope or list, with photos and routes), 90 days of history, Home Assistant, home lab, Wi-Fi and network diagnostics, system status and settings. Auto paging through chosen pages, an overnight sleep mode, and logs on an SD card.
- **Feather ESP32-S3 Reverse TFT** (240 x 135): nine auto-rotating pages driven by three buttons, with a battery gauge and status LED.

Both serve a local web dashboard (`http://weather7.local` or `http://weather.local`) with interactive maps, charts, history downloads, settings and logs.

The CrowPanel doubles as a **portable Wi-Fi tester**: on a USB power bank it shows live signal strength, every network in range, and runs latency, jitter, packet-loss and speed tests. A long-running monitor catches lag and drop-outs that come and go.

All data sources are free and need no API keys. See [weather-display/README.md](weather-display/README.md) to build and flash, and [weather-display/docs/DESIGN.md](weather-display/docs/DESIGN.md) for the architecture.

### Quick start

```bash
pip install platformio
cd weather-display
cp include/secrets.example.h include/secrets.h   # then add your Wi-Fi details
pio run -e crowpanel_7 -t upload
```

Set your location and regions in `weather-display/include/config.h` first.

## Repository layout

```
.github/workflows/ci.yml   Builds both boards, checks flash size, runs cppcheck and a secret scan
weather-display/
  include/                 Shared headers and compile-time configuration
  src/core/                Data feeds, history, settings, web server, network tests, logging
  src/crowpanel/           CrowPanel 7" display, touch and pages
  src/feather/             Feather display, buttons, battery and pages
  docs/DESIGN.md           Architecture, concurrency, hardware notes, design decisions
  scripts/                 Flash-size check, Windows flash helper, build tweaks
```

## Continuous integration

Every push and pull request builds both boards, fails if a firmware image passes 98% of its flash slot, runs `pio check` (cppcheck) and scans the history for committed secrets. `include/secrets.h` is git-ignored and must never be committed.

## Licence

[MIT](LICENSE)
