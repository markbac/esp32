# ESP32-S3 Weather Station

Home weather station firmware for two displays that share one core:

- **Elecrow CrowPanel 7" Basic** (800 × 480 touch): eighteen touch pages in seven groups, with sub-tabs, swipe navigation, auto paging and a sleep mode.
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
- Nearby flights with **trails**. On the CrowPanel, **tap any plane** for its **photo**, route and details, switch to a **list of every aircraft, nearest first**, and set the **radar radius** (5 to 100 nm) with - / +.
- **Wi-Fi and network diagnostics** (CrowPanel): live signal meter, every network in range with 3 hours of signal history, a **channel map and air survey** (how many devices are busy on your Wi-Fi, neighbours on your channel, air time used, re-sent frames, beacon overhead, busiest devices), a 30-second latency / jitter / packet-loss / speed test, and a 15 minute to 8 hour **monitor** that catches intermittent lag, drop-outs (with the reason) and moves between mesh nodes. Run it from a USB power bank to check any room.
- **Logs on an SD card** (CrowPanel): a daily debug log and a daily network CSV, viewable and downloadable from the web dashboard.
- **Home Assistant** summary (temperatures, people, heating, power, lights, open doors).
- **Home lab**: NAS health (Glances), internet speed (Speedtest Tracker), services (Uptime Kuma), Sonos now playing.
- System status.

**Also:** quarter-hourly history saved to flash (**90 days** on the CrowPanel, 8 on the Feather) with records, summaries and charts, and a local **web dashboard** with its own pages for every feature: interactive radar and flight maps, charts, aircraft photos, history up to 90 days (CSV download), plus a Mirror tab showing the device screens. The dashboard is at `http://weather7.local` for the CrowPanel and `http://weather.local` for the Feather.

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

## Settings

Open `http://weather7.local/settings` (or `weather.local` for the Feather) to set the Home Assistant URL and token, home-lab hosts and ports, Sonos speakers, backlight levels, auto page (on/off, seconds per page, which pages), sleep mode (on/off, sleep and wake times), the flight radius, Wi-Fi and an optional settings password. Secrets are **write-only**: they are never shown or returned, only reported as set/not set.

On the CrowPanel, **System > Settings** shows and changes auto page, the pages it rotates through, sleep mode and backlight by touch. The flight radius is set on the **Flights** page. Changes are saved to flash a few seconds after the last tap.

## Network tests

On the CrowPanel, open **Network**:

| Tab | What it does |
|---|---|
| Signal | Live strength of your Wi-Fi here (updates every second), every network in range and 3 hours of signal history. |
| Crowding | Channel map of every 2.4 GHz network (yours, including all mesh nodes and guest/IoT SSIDs, and the neighbours') with the quietest channel, plus a 20 s **air survey**: devices actually sending data on your Wi-Fi, other devices on the channel, how busy the air is, how often frames are re-sent, how much air your access points' beacons use, and the busiest devices. |
| Test | About 30 s: 20 pings to your router (this room's Wi-Fi), 20 to 1.1.1.1 (your broadband), a DNS lookup and an 8 s download. Gives a verdict and says whether the Wi-Fi or the internet is to blame. |
| Monitor | For problems that come and go: runs 15 minutes to 8 hours, pinging the router and the internet every second and listening to the channel. Each minute records ping, lost pings, spikes over 200 ms, drop-outs, signal, mesh moves, DNS time, active devices, air busy and re-sends. Views: **Ping**, **Signal**, **Air** and **Events** (e.g. "Wi-Fi dropped: beacon timeout", "Lag spike 1007 ms to the internet"). |
| Help | What each number means. |

Run the panel from a USB power bank to test the room where the problem is. Results are saved to the SD card and shown at `http://weather7.local/logs`, where you can also start a test, an air survey or a monitor from your phone. `http://weather7.local/api/wifi.json` lists every network with its signal history. The download speed is what the panel itself manages (about 1 to 10 Mbps), so use it to compare rooms rather than to check your broadband.

The panel's radio is **2.4 GHz only**: it cannot see 5 GHz networks or devices. The air survey only reads frame headers (addresses, size, speed); the content is encrypted and never read.

## Logs (SD card)

Put a FAT32 micro SD card in the CrowPanel's TF slot. Each day it writes:

- `/logs/app-YYYY-MM-DD.log`: the firmware's debug log with timestamps
- `/logs/net-YYYY-MM-DD.csv`: Wi-Fi scans, test results, monitor minutes and drop-outs

Files older than 60 days are deleted. `http://weather7.local/logs` lists them for viewing or download and shows a live log (the live log works without a card too).

Each boot logs why the board restarted. After a crash it also logs the crashed task and its backtrace (from the core dump in flash); decode it with `xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/crowpanel_7/firmware.elf <addresses>`.

## Controls

**CrowPanel:**
- Tap a bottom tab to open a group; tap a sub-tab at the top (or the same bottom tab again) to change page within it. Swipe left/right to step through every page.
- **Sleep mode:** touch the dark screen to wake it for a minute.
- **Radar:** tap the map to pause. While paused, tap the left or right third to step frames, or the middle to resume.
- **Flights:** tap a plane to select it. **List** shows every aircraft nearest first (drag to scroll, tap a row to select). **- / +** change the radius.
- **Network:** tap a network on Signal to highlight it; Run test; pick a duration and Start a Monitor.
- **History:** tap Summary, 24 h, 7 d, 30 d or 90 d.

**Feather:**

| Button | Action |
|---|---|
| D0 | Next page |
| D1 | Previous page |
| D2 | Refresh all data now |

## Documentation

See [docs/DESIGN.md](docs/DESIGN.md) for the architecture (C4 diagrams), concurrency, Tempest handling, hardware notes and design decisions. Run `doxygen Doxyfile` for API docs.
