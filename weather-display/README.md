# ESP32-S3 Weather Station

Home weather station firmware for two displays that share one core:

- **Elecrow CrowPanel 7" Basic** (800 × 480 touch): nine touch pages with a tab bar and swipe navigation.
- **Adafruit Feather ESP32-S3 Reverse TFT** (240 × 135): nine auto-rotating pages, driven by three buttons.

**Data shown:**
- Live readings from a **WeatherFlow Tempest** on your network (UDP broadcasts): temperature, humidity, pressure and trend, live wind compass, rain today, UV, solar, lightning.
- Hourly and 7-day forecast.
- Animated rain radar with a 2-hour "rain soon" forecast.
- Air quality, UV and pollen.
- Met Office warnings and AuroraWatch UK aurora alerts.
- Sunrise, sunset and moon phase.
- Nearby flights. On the CrowPanel, **tap any plane** to see its route and details.
- System status.

**Also:** quarter-hourly history saved to flash (8 days) with on-device charts, and a local web dashboard that mirrors every screen and charts the last 7 days (CSV download included). The dashboard is at `http://weather7.local` for the CrowPanel and `http://weather.local` for the Feather.

All data sources are free and need no API keys: Tempest (local), Open-Meteo, Met Office, AuroraWatch UK, RainViewer, OpenStreetMap, adsb.lol, adsbdb.

## Build

1. Install [PlatformIO](https://platformio.org/) (`pip install platformio`).
2. Copy `include/secrets.example.h` to `include/secrets.h` and enter your Wi-Fi details.
3. Set your location, Met Office region, county and Tempest elevation in `include/config.h`.
4. Build and flash:

```bash
pio run -e crowpanel_7 -t upload
```

```bash
pio run -e feather_s3_reverse_tft -t upload
```

On Windows, `scripts/flash.ps1` handles the Feather switching to a new COM port partway through an upload.

## Controls

**CrowPanel:**
- Tap a tab, or swipe left/right, to change page.
- **Radar:** tap the map to pause. While paused, tap the left or right third to step frames, or the middle to resume.
- **Flights:** tap a plane to select it.
- **History:** tap 24 h / 3 days / 7 days.

**Feather:**

| Button | Action |
|---|---|
| D0 | Next page |
| D1 | Previous page |
| D2 | Refresh all data now |

## Documentation

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture (C4 diagrams), concurrency, Tempest handling, hardware notes and design decisions. Run `doxygen Doxyfile` for API docs.
