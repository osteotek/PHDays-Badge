# PHDays Badge Source Code

![LAST0063](https://github.com/user-attachments/assets/c6ef8c73-12f9-4978-b324-96bd69605260)

PHDays Badge is an interactive device made by Positive Labs for cybersecurity festival PHDays Fest. It can display images and animations on a 10x10 pixel screen. There is a set of some standard, but you can connect to it via Wi-Fi and draw your own image or animation. It also can play RTTTL melodies and has built-in RTTTL-editor.

WebUI is located in separate [PHDays-Badge-WebUI](https://github.com/nlef/PHDays-Badge-WebUI) repository.

3D models of the badge and its body details can be found in the [PHD_Badge_2025 models](https://github.com/Ushinbuy/PHD_Badge_2025) repository.

## Home Wi-Fi firmware

This version keeps a persistent connection to a 2.4 GHz WPA2-compatible home
network while retaining the badge's password-protected `phd2_…` hotspot.

1. Join the badge hotspot and open `http://192.168.4.1/wifi` (also linked from the editor).
2. Enter your network name and password, then choose **Save and connect**.
3. After the badge restarts, reconnect to its hotspot and reload the Wi-Fi page.
   It displays the home-network IP address once connected.
4. Join your home network and open that IP address to use the pixel and music editors.
   The router may list the badge as `phdays-badge`; a DHCP reservation keeps its IP stable.

Credentials are stored in the badge's NVS flash, not in this repository. They are
never returned by the settings API. To change them, reconnect to the badge hotspot;
configuration requests from the home-network interface are rejected. If the router
is unavailable or the password is wrong, the hotspot remains available and the badge
retries the home connection every five seconds. WPA3-only and 5 GHz-only networks
are not supported by this configuration.

The editor uses same-origin API requests, so it works through either interface.
Automatic festival OTA downloads and the periodic Wi-Fi disconnect task are disabled.
USB flashing remains available. The existing display, sound, buttons, and saved-project
features are retained.

### Build and install

Use ESP-IDF 5.4.1 with the ESP32 toolchain, then run `idf.py build`.
Before installing on an existing badge, back up its flash and inspect its partition
table. Preserve NVS and the `storage` partition; do not erase the whole chip.
Flash the application into the active OTA application partition only after checking
its offset and size against the connected device. The home IP is also printed on
the USB serial console at 115200 baud after DHCP succeeds.
