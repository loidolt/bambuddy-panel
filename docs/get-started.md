# Get started

## What you need

You’ll need a [board with a firmware profile](boards.md), a 5 V USB supply, a USB **data** cable,
and a computer to flash the board. Have a phone or computer handy for setup. The panel needs a 2.4
GHz WiFi network from which it can reach the Bambuddy server over HTTP.

## Flash the firmware

**From source.** Install PlatformIO Core and follow
[Build and flash](development.md#build-and-flash). Use this route if there is no published firmware
file for your board. For an ESP32-2432S028R, check whether the display uses an ST7789 or an ILI9341
controller. The two variants need different builds.

**From a published release.** If the
[releases page](https://github.com/loidolt/bambuddy-panel/releases) has firmware for your board,
download the matching `-full.bin` and `SHA256SUMS.txt`. Compare the file’s
[SHA-256 checksum](updating.md#verify-a-download), then connect the board and write the full image
at address `0x0` with
[esptool v5](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html). Replace
the example filename with your download’s name.

```bash
esptool --chip esp32 write-flash 0x0 "your-board-full.bin"
```

Writing the full image erases saved WiFi, Bambuddy settings and touch calibration. For an existing
panel, [update from its settings page](updating.md) to keep them.

## Create an API key

In Bambuddy, open **Settings → API Keys** and create a dedicated key. For a display-only panel,
select **Read Status** and clear the other scopes. Enable additional scopes for the actions you want
at this printer.

| Key scope | What the panel can request |
|---|---|
| Read Status | Read printer status, queue, print history, settings, maintenance and spool inventory |
| Control Printer | Pause, resume, stop, confirm plate clear or clear a printer error |
| Manage Queue | Start a staged job or resume held jobs |
| Manage Archives | Record a print outcome |
| Manage Maintenance | Mark a maintenance task done |
| Manage Inventory | Assign a spool to a slot or remove its assignment |

The key also inherits its owner’s access. Starting someone else’s job requires permission to manage
all queue jobs, and review restrictions still apply. Resuming the queue requires permission to
manage all jobs. Recording an outcome for someone else’s print requires permission to edit all
archives.

To restrict the key to one printer in Bambuddy 1.3.2, an administrator can use
`PATCH /api/v1/api-keys/{key_id}` with, for example, `{"printer_ids": [3]}` for printer 3. The panel
stores the key in flash and uses it over plain HTTP. See [Security](security.md).

## Set up a panel

1. **Calibrate touch if prompted.** On a new resistive panel, press each of the four crosses
   steadily and lift your finger between them. Capacitive panels skip this step.
2. **Join the panel’s setup network.** Scan the first QR code or join `BambuddyPanel-Setup` using
   the password on the screen. Choose your WiFi network and enter its password. If the network
   chooser doesn’t open, visit `http://192.168.4.1` while connected to the panel.
3. **Open the panel’s settings.** Once the panel joins your WiFi, connect your phone or computer to
   that network. Scan the second QR code, or open the panel address on the screen and enter the code
   beside it.
4. **Connect to Bambuddy.** Enter the server’s LAN address, for example `http://192.168.1.50:3700`,
   and paste the API key. Choose your printer from the suggestions or enter its numeric Bambuddy ID.
   The panel lists up to 16 printers visible to the key.
5. **Select Save.** The panel checks the printer’s status and starts displaying it. If the server is
   temporarily unreachable or returns a server error, **Use these settings anyway** saves the values
   for a later connection. For an invalid key or printer ID, correct the indicated field and save
   again.

## Reopen settings

On the panel, tap the wrench to open **Maintenance**, then press **Hold for settings** until the bar
fills. The panel restarts into setup and displays a new code. Connection error screens also offer
**Open settings**.

- **Change the printer, key or server** in the browser. Leave the key blank to reuse it with the
  same address. Enter the key again when changing the address.
- **Change WiFi** in the browser forgets the saved network and restarts WiFi setup.
- **Calibrate touch** on the panel starts calibration on resistive boards. **Cancel** on the panel
  discards unsaved connection edits.

If touch is misaligned or your WiFi has changed, touch the screen during **Starting** at power-on.
On a resistive panel, release when prompted for settings, or keep holding for four seconds to
recalibrate. A capacitive panel goes straight to settings. If the saved WiFi is unreachable during
setup, the panel opens its setup network.
