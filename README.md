# PHDays Badge: desk info display

![LAST0063](https://github.com/user-attachments/assets/c6ef8c73-12f9-4978-b324-96bd69605260)

Firmware that turns the PHDays Fest badge by Positive Labs (ESP32, 10x10 RGB
matrix, piezo buzzer, three buttons, battery) into a small info display for the
desk: a clock, the current weather and a Pomodoro timer, configured from a web
page on the home network. It started from the original badge firmware, which
displayed pictures and animations drawn in a web editor.

3D models of the badge and its body details can be found in the
[PHD_Badge_2025 models](https://github.com/Ushinbuy/PHD_Badge_2025) repository.

## Screens

The badge rotates through the enabled screens, each for a configurable time:

- **Clock**: hours above minutes; the right column fills over each minute.
  Time comes from NTP in the configured POSIX time zone (default `MSK-3`).
- **Weather**: an icon (sun, moon, cloud, rain, snow, storm, fog) at the top
  left and the temperature with a degree sign at the bottom right (two-digit
  frosts like −12 fill the row and drop the degree sign). In the second half of
  its turn the screen shows today's forecast: the high above the low., from [Open-Meteo](https://open-meteo.com/) (no API key),
  refreshed every 15 minutes for the `[weather]` location in `wifi_secrets.ini`.
- **Timer**: while a Pomodoro timer runs it takes over the screen: minutes left
  (seconds in the last minute) over a shrinking bar, red for focus and green for
  the break. Each phase end plays the alert melody.

Between screens (at every rotation step, and when the timer starts or stops),
Matrix-style green rain sweeps the old screen away and reveals the next one; it
can be turned off on the web page.

**Messages** scroll across the screen, for example from CI or Home Assistant:
`POST /api/v1/notify` with `{"text": "Build passed", "color": "#00ff00", "repeat": 2,
"sound": true}` (color, repeat and sound are optional), `badge say "Build passed"`, or
the Message box on the web page. The font covers Latin and Russian letters
(lowercase is shown in capitals), digits, common punctuation and `°`; other
characters show as `?`. Messages
follow the screen switch and night mode, like the screens.

**Night mode** dims the LEDs, or switches them off, between two times (for
example 23:00 to 07:00) once the clock is set; it is configured on the web page or
with `badge night 23:00 07:00 1`.

Status images briefly replace the screens: update progress and result, and the
battery level.

| Button | Click | Hold | Double-click |
|---|---|---|---|
| Brightness (GPIO 17) | next brightness level | | |
| Timer (GPIO 13) | start/stop the Pomodoro timer | skip to the next phase | battery level |
| Screen (GPIO 16) | LEDs off/on | | next screen |

## Web page

Open `http://phdays-badge.local/` (mDNS), or the badge's IP address: the router
lists it as `phdays-badge`, and it is printed on the USB serial console. The page shows the time, weather and timer
with start/skip/stop buttons, and edits the settings: enabled screens, seconds per
screen, brightness, time zone, focus and break minutes, and the alert melody. The
melodies section lists saved RTTTL melodies, previews them in the browser, plays
them on the badge, and saves or deletes them.

API, all JSON under `/api/v1`: `GET status`, `GET`/`POST settings` (partial
updates are fine), `POST timer` with `{"action": "start" | "stop" | "skip"}`, `POST screen` with
`{"action": "on" | "off" | "toggle" | "next"}`, `POST notify` (see Messages above),
`GET`/`POST buzzer/melodies` and `POST buzzer/melody` to play one.

## Command line

`scripts/badge` controls the badge from a terminal (needs `curl` and `jq`):

```sh
scripts/badge status              # time, weather, timer, screen, battery
scripts/badge timer start         # also: stop, skip, watch (live countdown)
scripts/badge screen off          # also: on, toggle, next
scripts/badge brightness 12
scripts/badge night 23:00 07:00 0  # LEDs off at night; also: night on|off
scripts/badge set focus_minutes=50 break_minutes=10
scripts/badge play Alert          # a saved melody, or RTTTL text
scripts/badge say -c '#00ff00' "Build passed"
scripts/badge update              # build and install the firmware over Wi-Fi
```

The badge address comes from `-H` or `BADGE_HOST` (default `phdays-badge.local`). To use it
from anywhere: `ln -s "$PWD/scripts/badge" ~/.local/bin/badge`.

## Wi-Fi

The badge joins a 2.4 GHz WPA2-compatible home network from `wifi_secrets.ini`.
Its open `phd2_…` hotspot is only a fallback:

1. Copy `wifi_secrets.example.ini` to `wifi_secrets.ini` (gitignored) and fill in
   the network, a Wi-Fi update token and the weather location. The build compiles
   the values into the firmware.
2. Build and flash (see below). The badge joins your network on boot.

If home Wi-Fi has no connection for 2 minutes (wrong password, router off,
different location), the hotspot turns on. Join it and open `http://192.168.4.1/wifi`
to enter another network; the badge saves it to NVS flash and restarts. Settings
saved this way take precedence over `wifi_secrets.ini`. The hotspot turns off again
as soon as home Wi-Fi connects; while it is on, the badge retries home Wi-Fi every
30 seconds. Without any configured network, the badge runs the hotspot only.
While the hotspot is on, anyone nearby can join it and change the Wi-Fi network;
firmware updates still need the `[ota] token`.

Credentials are never logged or returned by the API, and Wi-Fi configuration
requests from the home-network interface are rejected. WPA3-only and 5 GHz-only
networks are not supported.

## Build and install

The firmware builds with [PlatformIO](https://platformio.org/). `platformio.ini`
pins `espressif32@7.1.3`, which provides ESP-IDF 6.1.0; PlatformIO downloads the
framework and toolchain on the first build.

```sh
pio run                  # build
pio run -t upload        # flash bootloader, partition table, otadata and app
pio device monitor       # serial console at 115200 baud
```

`pio run -t upload` writes the bootloader (`0x1000`), the partition table (`0x8000`),
a blank otadata (`0x19000`, so the badge boots `ota_0`) and the app (`ota_0`,
`0x20000`). It never writes NVS or `storage`, so settings, Wi-Fi credentials and
saved melodies survive. Do not erase the whole chip. Before changing
`partitions.csv`, back up the flash: moving NVS or `storage` loses their contents.

Uploads run at 115200 baud because the badge's CH340 USB-serial adapter drops bytes
at higher rates on macOS.

### Updating over Wi-Fi

After one USB flash, later builds can be installed over Wi-Fi:

```sh
pio run -e badge-ota -t upload                          # badge at phdays-badge.local
pio run -e badge-ota -t upload --upload-port <badge IP>
```

This needs an `[ota] token` in `wifi_secrets.ini`; without one, Wi-Fi updates are
disabled. The upload goes to the inactive app slot (`ota_0`/`ota_1`), is verified,
and the badge restarts into it. While it runs, the screen shows a scrolling download
arrow, then fills one pixel per percent; after the restart a check mark appears. The
new firmware keeps itself only once it is reachable for the next update again,
through home Wi-Fi or, if that is out of range, the fallback hotspot; otherwise it
shows a cross after 3 minutes and rolls back to the previous firmware, so a broken
build cannot lock out Wi-Fi updates. Resetting the badge in the first seconds after
an update also rolls it back. The token is sent over plain HTTP on the home network.
Wi-Fi updates replace the app only; bootloader and partition-table changes need USB.

### Web UI

The settings page is a small Vite + TypeScript app in `webui/` (Node.js 22.12 or
newer), built into one self-contained page that the firmware embeds as
`main/ui/index.html.gz`:

```sh
python3 scripts/build_webui.py   # npm ci (first run), test, build, gzip into main/ui/
pio run -e badge-ota -t upload   # embed and install it
```

For UI work, `npm run dev` in `webui/` starts a dev server that proxies the API to
the badge (`BADGE_HOST=<address>` to use another one). `main/ui/wifi.html` (the hotspot Wi-Fi page) is
plain HTML and not built from `webui/`.

### Tests

```sh
cd webui && npm test                          # web UI logic
node --test tests/wifi-ui.test.cjs            # hotspot Wi-Fi page
cc -std=c11 -Wall -Wextra -Imain tests/screens_test.c main/screens.c -lm -o /tmp/screens_test && /tmp/screens_test --preview
```

The screens test runs the matrix drawing code on the host and, with `--preview`,
prints every screen as ASCII art. GitHub Actions (`.github/workflows/ci.yml`) runs
all of these on every push, builds the firmware, and checks that
`main/ui/index.html.gz` was rebuilt after changes in `webui/`.

### Configuration

The ESP-IDF configuration is `sdkconfig.defaults`, which holds only non-default
options; `sdkconfig` is generated from it on the first build and is not committed.
After changing options with `pio run -t menuconfig`, regenerate the defaults with
ESP-IDF's `save-defconfig` target
(`~/.platformio/packages/tool-ninja/ninja -C .pio/build/badge save-defconfig`) and
re-add `CONFIG_CJSON_NESTING_LIMIT=16`, which that target drops.
