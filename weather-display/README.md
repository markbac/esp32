# ESP32-S3 Weather Station

Home weather station firmware for two displays that share one core:

- **Elecrow CrowPanel 7" Basic** (800 × 480 touch): ten touch pages with a tab bar and swipe navigation.
- **Adafruit Feather ESP32-S3 Reverse TFT** (240 × 135): nine auto-rotating pages, driven by three buttons.

**Data shown:**
- Live readings from a **WeatherFlow Tempest** on your network (UDP broadcasts): temperature, humidity, pressure and trend, live wind compass, rain today, UV, solar, lightning.
- Hourly and 7-day forecast.
- Animated rain radar with a 2-hour "rain soon" forecast.
- Air quality, UV and pollen.
- **Grid carbon intensity** for North West England, generation mix and the greenest time to use power.
- **Frost & ice risk**, **rain rate**, and **rain arrival** estimated by tracking the radar.
- Met Office warnings and AuroraWatch UK aurora alerts.
- Sunrise, sunset and moon phase.
- Nearby flights with **trails**. On the CrowPanel, **tap any plane** for its **photo**, route and details.
- System status.

**Also:** quarter-hourly history saved to flash (**90 days** on the CrowPanel, 8 on the Feather) with records, summaries and charts, and a local web dashboard that mirrors every screen and charts the last 7 days (CSV download included). The dashboard is at `http://weather7.local` for the CrowPanel and `http://weather.local` for the Feather.

All data sources are free and need no API keys: Tempest (local), Open-Meteo, Met Office, AuroraWatch UK, RainViewer, OpenStreetMap, adsb.lol, adsbdb, planespotters.net, carbonintensity.org.uk. Aircraft photos are decoded with [stb_image](https://github.com/nothings/stb) (public domain).

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
- **History:** tap Summary, 24 h, 7 d, 30 d or 90 d.

**Feather:**

| Button | Action |
|---|---|
| D0 | Next page |
| D1 | Previous page |
| D2 | Refresh all data now |

## Documentation

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture (C4 diagrams), concurrency, Tempest handling, hardware notes and design decisions. Run `doxygen Doxyfile` for API docs.
