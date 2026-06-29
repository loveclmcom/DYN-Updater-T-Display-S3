# DYN Updater by LOVECLM.COM

**An ESP32-based DynDNS client with a touchscreen display and browser-based configuration.**

![Version](https://img.shields.io/badge/version-1.1-blue) ![Platform](https://img.shields.io/badge/platform-ESP32-green) ![License](https://img.shields.io/badge/license-MIT-orange)

---

## Why This Exists

Keeping a DynDNS hostname up to date usually requires a dedicated computer running 24/7, or a router with built-in DynDNS support — neither of which is always available. Router firmware can be unreliable, and not everyone has a machine that never sleeps.

An ESP32 development board changes that equation entirely. It is inexpensive, widely available, draws very little power, and requires no operating system. This project turns one into a standalone DynDNS updater with a proper display and a clean web interface for configuration — no soldering, no Linux, no always-on PC required.

---

## Features

- **Touchscreen UI** — dark-themed 320×240 display shows public IP, DynDNS status, hostname, and update countdown at a glance
- **Browser configuration** — change WiFi credentials, DynDNS account details, and update interval from any device on the same network; no reflashing needed
- **Automatic updates** — checks and updates your DynDNS hostname at a configurable interval (5, 10, 30, or 60 minutes)
- **Manual update** — tap the STATUS card on screen to force an immediate update
- **Smart change detection** — only calls the DynDNS API when your public IP has actually changed, avoiding abuse flags
- **DynDNS IP comparison** — shows the IP currently registered on DynDNS alongside your public IP so you can spot mismatches instantly
- **First-boot AP mode** — device broadcasts its own Wi-Fi access point when no credentials are saved, so you can configure it from your phone or laptop without touching any code
- **Screen rotation** — tap the rotation button to flip the display 180°, useful depending on how the device is mounted
- **Persistent settings** — all configuration is saved to flash and survives power cycles

---

## Hardware

This sketch targets the **Guition ESP32-2432S028**, commonly known as the **Cheap Yellow Display (CYD)**. It is a compact all-in-one ESP32 development board with a 2.8-inch ILI9341 TFT and an XPT2046 resistive touchscreen built in.

| Item | Detail |
|---|---|
| Board | Guition ESP32-2432S028 (CYD) |
| Display | ILI9341, 320×240, SPI (HSPI bus) |
| Touch | XPT2046 resistive, SPI (VSPI bus) |

### Display Pin Mapping

| Signal | GPIO |
|---|---|
| CS | 15 |
| DC | 2 |
| RST | — (not used) |
| MOSI | 13 |
| SCK | 14 |
| MISO | 12 |
| Backlight | 21 |

### Touchscreen Pin Mapping

| Signal | GPIO |
|---|---|
| CS | 33 |
| IRQ | 36 |
| CLK | 25 |
| MISO | 39 |
| MOSI | 32 |

> These are the default pins on the CYD board. If you are using a different ESP32 board, adjust the `#define` values at the top of `DYN_Loveclm.ino`.

---

## Required Libraries

Install all three via **Arduino IDE → Tools → Manage Libraries**:

| Library | Author |
|---|---|
| Adafruit ILI9341 | Adafruit |
| Adafruit GFX Library | Adafruit |
| XPT2046_Touchscreen | Paul Stoffregen |

The following libraries are part of the ESP32 Arduino core and require no separate installation:

`WiFi` · `WiFiClientSecure` · `WebServer` · `HTTPClient` · `Preferences` · `ESPmDNS`

---

## Board Setup in Arduino IDE

1. Add the ESP32 board package if you have not already:  
   **File → Preferences → Additional Boards Manager URLs**, add:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

2. **Tools → Board → ESP32 Arduino → ESP32 Dev Module**

3. Recommended upload settings:
   - Flash Size: **4MB**
   - Partition Scheme: **Default 4MB with spiffs** (or any scheme with enough app space)
   - Upload Speed: **921600**

---

## Installation

1. Download `DYN_Loveclm.ino` from the [Releases](../../releases) page
2. Open it in Arduino IDE
3. Install the required libraries listed above
4. Select your board and port
5. Click **Upload**

---

## First-Time Setup

On the very first boot (or any time no WiFi credentials are saved), the device starts in **Access Point mode**:

1. On your phone or laptop, connect to the WiFi network named **`LOVECLM-DYN-Updater`** (no password)
2. Open a browser and go to **`http://192.168.4.1`**
3. Fill in the Settings form:
   - **WiFi SSID / Password** — your home or office network
   - **DynDNS Username / Password** — your [dyn.com](https://account.dyn.com) account credentials
   - **DynDNS Hostname** — the hostname you want to keep updated (e.g. `yourhome.dyndns.org`)
   - **Update Interval** — how often to check and update (5 / 10 / 30 / 60 minutes)
4. Click **Save Settings**
5. The device restarts, connects to your WiFi, and begins updating automatically

After setup, you can reach the web UI from any device on the same network at:
- `http://<device-IP>` (IP shown on the device screen)

---

## Screen Layout

```
┌──────────────────────────────────────────┐
│  DYN Updater          [↻]  [WiFi  85%]  │  ← Header
├──────────────────────────────────────────┤
│                                          │
│           YOUR PUBLIC IP                 │  ← IP Card
│            1.2.3.4                       │
│                                          │
├────────────────────┬─────────────────────┤
│  HOSTNAME          │  STATUS             │
│  yourhome          │  ┌───────────┐      │  ← Row 2
│  .dyndns.org       │  │  UPDATED  │      │
│  DYN: 1.2.3.4 ●   │  └───────────┘      │
├────────────────────┼─────────────────────┤
│  NEXT UPDATE       │  INTERVAL           │  ← Row 3
│  04:32             │  10 mins            │
├──────────────────────────────────────────┤
│  Web UI: http://192.168.1.x        v1.1  │  ← Footer
└──────────────────────────────────────────┘
```

**DYN IP indicator colour:**
- 🟢 Green — DynDNS record matches your current public IP (all good)
- 🟡 Yellow — DynDNS record is different from your public IP (tap STATUS to update)

---

## Touch Controls

| Area | Action |
|---|---|
| **STATUS card** | Force an immediate DynDNS update |
| **INTERVAL card** | Cycle update interval: 5 → 10 → 30 → 60 mins |
| **↻ button** (top-right) | Toggle screen rotation 0° / 180° |

> Tapping STATUS more than once within 30 seconds shows a **"PLEASE WAIT"** warning. This is intentional — frequent unnecessary updates can trigger DynDNS abuse prevention and get your account blocked.

---

## Web Interface

The built-in web page is served directly from the ESP32. Open it in any browser:

- **Dashboard tab** — shows current public IP, DynDNS status, hostname, last update time, and a manual Update Now button
- **Settings tab** — configure WiFi, DynDNS credentials, and update interval; password fields have a show/hide toggle
- **WiFi scanner** — scans for nearby networks and populates the SSID field from a dropdown

---

## DynDNS Account

This project is designed for use with [Dyn](https://account.dyn.com) (formerly DynDNS). You will need:

- A free or paid Dyn account
- At least one hostname registered under your account

The updater uses the standard Dyn update API:  
`https://members.dyndns.org/nic/update?hostname=<host>&myip=<ip>`

---

## Versioning

| Version | Notes |
|---|---|
| v1.1 | Initial public release |

Version increments by 0.1 each release (v1.1 → v1.2 → … → v1.9 → v2.0).

---

## License

MIT License — free to use, modify, and distribute. Attribution appreciated but not required.

---

## Credits

Designed and built by **[LOVECLM.COM](https://www.loveclm.com)**
