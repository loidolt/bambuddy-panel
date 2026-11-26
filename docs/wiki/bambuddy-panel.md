---
title: Bambuddy Panel
description: Per-printer touch panel for Bambuddy on inexpensive ESP32 touch displays
---

# Bambuddy Panel

A small touch panel that sits next to a printer and shows what Bambuddy knows about it, with the few actions you want while standing there. One panel per printer. It runs on the inexpensive ESP32-2432S028R ("Cheap Yellow Display") and talks only to your Bambuddy server over WiFi &mdash; never to the printer or to any cloud &mdash; so it works with every printer Bambuddy supports.

The screen follows the printer's state: progress, time left, layer and temperatures while printing; the *Ask for outcome* question with a thumbs up / thumbs down when a print finishes; **Plate is clear** when Bambuddy waits for it; the next queued jobs, and why the first one is waiting, when idle. Anything that starts a print or can't be undone needs a 1.5 s hold, so a stray touch can't stop a print or start one on a dirty plate.

**Author:** [loidolt](https://github.com/loidolt)
**Repository:** [github.com/loidolt/bambuddy-panel](https://github.com/loidolt/bambuddy-panel)
**Status:** Independent community project, early

!!! warning "Early"

    The author flags the panel as early. It runs on the ESP32-2432S028R with the ST7789 display; the build profiles for the other boards have not been flashed yet. Expect rough edges.

---

## What it does

- **Print controls** &mdash; pause, resume and stop (hold).
- **Plate is clear** (hold), offered when *Require plate clear* is on; says so when clearing will start the next print.
- **Outcome question** &mdash; thumbs up, thumbs down or skip, asked before the plate is released.
- **Queue** &mdash; start the next job staged for this printer (hold), resume a queue held after a failed print (hold).
- **Errors** &mdash; shows the HMS error's text and clears it.
- **Filament** &mdash; the printer's slots with the inventory spool assigned to each and what is left on it; assign or remove a spool (hold). Bambuddy's own inventory, not Spoolman.
- **Maintenance** &mdash; the printer's tasks, most urgent first; mark one done (hold).
- **Status LED** &mdash; blinks when someone should come over: outcome question, plate to clear, paused, error, Bambuddy unreachable.

---

## What you need

- An ESP32-2432S028R ("Cheap Yellow Display": ESP32, 2.8" 320x240 resistive touchscreen, no PSRAM) and a 5 V USB supply. Build profiles also exist for its 2.4", 3.2" and 3.5" relatives, the Guition JC2432W328C and the WT32-SC01; those are untested.
- A data-capable USB cable for flashing.
- A Bambuddy [API key](../features/api-keys.md). The panel's settings page lists the printers the key may see, so there is no printer ID to look up.

---

## Setup

1. Flash a firmware file from the [releases page](https://github.com/loidolt/bambuddy-panel/releases) with `esptool`, or build with PlatformIO. The [README](https://github.com/loidolt/bambuddy-panel#flash-a-release) has both.
2. Create an API key for the panel with **Read Status**, **Control Printer** and **Manage Queue**; optionally **Manage Archives** (outcome question), **Manage Maintenance** and **Manage Inventory**. Without a permission the panel says which one the key lacks, and the rest works.
3. Power the panel. It asks for four taps to calibrate the touch, then shows a QR code for its setup network, where you choose your WiFi.
4. Once on your WiFi it shows a second QR code for its settings page. Enter the Bambuddy URL and the API key, pick the printer from the list, save.

---

## How it talks to Bambuddy

Bambuddy Panel uses Bambuddy's documented [REST API](../reference/api.md), authenticated with an [API key](../features/api-keys.md) in the `X-API-Key` header. It polls the printer's status, its queue and its last archive about every 4 seconds, and maintenance, settings and spool assignments once a minute. No internal endpoints, no MQTT, no connection to the printer. The README lists [every request it makes](https://github.com/loidolt/bambuddy-panel#how-it-talks-to-bambuddy).

The connection is plain HTTP on your LAN, and the key is stored unencrypted in the panel's flash. Give the key only the permissions above and delete it in Bambuddy if a panel goes missing.

---

## Support

Issues, feature requests and questions about the panel belong on the [project's GitHub repository](https://github.com/loidolt/bambuddy-panel/issues). The Bambuddy team doesn't maintain Bambuddy Panel and can't ship fixes for it.

For Bambuddy-side questions (API key setup, permissions, etc.), the [Discord](https://discord.gg/aFS3ZfScHM) is the right place to ask.
