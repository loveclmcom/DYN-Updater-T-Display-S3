# LOVECLM DYN Updater

**DynDNS updater for ESP32 devices** — keeps your DynDNS hostname pointed at your current public IP, with a dark web UI for configuration.

Built by [LOVECLM.COM](https://www.loveclm.com)

---

## Variants

### DYN_Loveclm — Guition ESP32-2432S028 (CYD)
- ILI9341 320×240 display via HSPI
- XPT2046 touchscreen
- Touch to force update, cycle interval, or flip screen rotation
- Libraries: `Adafruit_ILI9341`, `Adafruit_GFX`, `XPT2046_Touchscreen`

### DYN_Loveclm_T3 — Lilygo T-Display-S3
- ST7789 320×170 display via 8-bit parallel (TFT_eSPI)
- Two physical buttons: front-left = cycle interval, front-right = force update
- No touch, no rotation button
- Library: `TFT_eSPI` with `Setup206_LilyGo_T_Display_S3.h`

---

## Features

- Automatic public IP detection via [api.ipify.org](http://api.ipify.org)
- DynDNS update via `members.dyndns.org`
- Configurable update interval (5 / 10 / 30 / 60 min)
- AP setup mode when no WiFi credentials are saved
- mDNS: `http://LOVECLM-DYN-Updater.local`
- Dark web UI — dashboard + settings, WiFi scan, manual update trigger
- NVS flash storage for all settings (survives power cycle)
- 30-second cooldown on manual updates to avoid DynDNS abuse blocks

---

## Setup

### First boot (no credentials saved)

1. Device starts in **AP mode** — connect your phone/PC to:
   - SSID: `LOVECLM-DYN-Updater`
   - Password: `12345678`
2. Open browser at `http://192.168.4.1`
3. Go to **Settings** → enter your WiFi SSID/password and DynDNS credentials → **Save**
4. Device restarts and connects to your WiFi

### Web UI

Once connected, access the dashboard at `http://<device-ip>` or `http://LOVECLM-DYN-Updater.local`

---

## T-Display-S3 TFT_eSPI Setup

In `Documents/Arduino/libraries/TFT_eSPI/User_Setup_Select.h`:

```cpp
// Comment out:
//#include <User_Setup.h>

// Uncomment:
#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>
```

**Arduino IDE board settings:**

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| PSRAM | OPI PSRAM |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 16M Flash (3M APP/9.9MB FATFS) |
| Upload Mode | UART0/Hardware CDC |

---

## License

MIT — free to use, modify, and distribute.

---

*Contact: [contact@loveclm.com](mailto:contact@loveclm.com)*
