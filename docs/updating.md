# Updating

A panel that runs Bambuddy Panel updates without USB, and keeps its settings and touch
calibration:

1. Download your build's `-app.bin` from the
   [releases](https://github.com/loidolt/bambuddy-panel/releases) (not the `-full.bin`). The settings page
   names the build the panel runs.
2. Tap the wrench, then press **Hold for settings** until the bar fills
   ([Set up a panel](get-started.md#set-up-a-panel)).
3. Under **Update firmware**, choose the file. The code on the panel's screen is needed, as for
   any change. The panel says "Updating", then restarts with the new firmware; it takes about
   a minute.

The uploader checks the application format, size and board marker before selecting the new boot slot. A wrong-board image or interrupted transfer leaves the current slot selected. Use a trusted release and verify its checksum.

## Recover over USB

Use a **full image for the exact board**. The following example writes the bootloader, partition table, boot selection and application. It clears WiFi, Bambuddy settings and touch calibration. Substitute the filename of your downloaded release.

```bash
esptool --chip esp32 write-flash 0x0 bambuddy-panel-0.1.0-cyd-st7789-full.bin
```

Writing an app image at a fixed offset can leave the other OTA slot selected.

Released firmware has pushed updates over WiFi (ArduinoOTA) switched off; for those, build your
own ([Development](development.md#pushed-updates-over-wifi)).

What this means for who can change a panel's firmware is under
[Security](security.md).
