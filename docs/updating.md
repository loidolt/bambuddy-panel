# Updating

A panel that runs Bambuddy Panel updates without USB, and keeps its settings and touch
calibration:

1. Download your build's `-app.bin` from the
   [releases](https://github.com/loidolt/bambuddy-panel/releases) (not the `-full.bin`). The settings page
   names the build the panel runs.
2. Open the panel's settings page: the wrench, then hold the **gear**
   ([Set up a panel](get-started.md#set-up-a-panel)).
3. Under **Update firmware**, choose the file. The code on the panel's screen is needed, as for
   any change. The panel says "Updating", then restarts with the new firmware; it takes about
   a minute.

The panel refuses a file built for another board, a `-full.bin`, and anything that isn't
Bambuddy Panel firmware, and keeps running what it had. If the upload is cut off, it does too.

The panel never fetches firmware itself; it still talks only to your Bambuddy. Over USB, the
same file goes to 0x10000:

```bash
esptool.py --chip esp32 write_flash 0x10000 bambuddy-panel-0.1.1-cyd-st7789-app.bin
```

That is also the way back if a new firmware starts but doesn't work: the panel doesn't return
to the old one by itself.

Released firmware has pushed updates over WiFi (ArduinoOTA) switched off; for those, build your
own ([Development](development.md#pushed-updates-over-wifi)).

What this means for who can change a panel's firmware is under
[Security](security.md).
