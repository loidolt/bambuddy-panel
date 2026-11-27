# Troubleshooting

## The picture

**White or garbled display on an ESP32-2432S028R.** Check the display controller. This board comes
in ST7789 and ILI9341 variants under the same model number. If the controller is unknown, try the
other profile over USB. For swapped colours, an inverted picture or reversed orientation, see
[board-specific adjustments](boards.md#if-the-picture-or-the-touch-is-wrong).

**The screen dims.** Tap once to wake the backlight, then tap the control. The panel dims
automatically on status screens that can be left unattended.

**Names look different.** The firmware converts many accented Latin letters to ASCII for display.
For example, *Jürgen* appears as *Jurgen*. Unsupported characters appear as `?`.

## The touch lands in the wrong place

On a resistive board, tap the wrench and press **Hold for settings** until the bar fills. On the
panel, choose **Calibrate touch**. Press each cross steadily and lift between presses.

If touch is too far off to use the buttons, touch the screen during **Starting** just after
power-on. Keep holding for about four seconds to enter calibration. A capacitive board uses its
configured touch orientation. See [Boards](boards.md#if-the-picture-or-the-touch-is-wrong) for
mirrored or unresponsive capacitive touch.

## WiFi and setup

- **The panel can’t join its saved WiFi.** A configured panel retries automatically. To switch
  networks, [reopen settings](get-started.md#reopen-settings) and choose **Change WiFi**. If the old
  network is already gone, enter settings at power-on to open the setup network.
- **The WiFi chooser didn’t open.** Join `BambuddyPanel-Setup` with the password currently on the
  screen, then open `http://192.168.4.1`. The setup network times out after ten minutes. On a fresh
  panel, setup restarts with a new password.
- **The browser asks for a code.** After WiFi setup, use the address and code on the panel’s second
  QR screen. Your browser must be able to reach the panel on its new network. If the code changes or
  the page times out, reopen settings and use the current code.

## Bambuddy connection and permissions

| What you see | What to check |
|---|---|
| Grey status label | One status request went unanswered after a working connection. The panel keeps the last reading and disables writes until a fresh status arrives. |
| “No answer from Bambuddy” over the last status | After two consecutive missed replies, the panel shows how long it has been disconnected. Check WiFi, then the Bambuddy server. Local navigation is still available. |
| Connection error with **Open settings** | A panel with no successful status yet can show this immediately. After a previously working connection, it appears after five minutes offline, or immediately for a rejected key, inaccessible printer or invalid URL. Use the server’s HTTP address and port. |
| API key refused or printer missing | Check that the key is enabled and valid, grants **Read Status**, and can see the configured printer ID. If Bambuddy runs on another computer, use that computer’s LAN address. |
| “Access denied” after a button press | Check both the named key scope and the owner’s permissions for that printer, job or archive. See the [permission table](get-started.md#create-an-api-key). |
| “No matching filament loaded” or “Not enough filament loaded” | Check the job’s filament requirements and the loaded or assigned spools in Bambuddy, then retry the start. |

## A control or reading is missing

- **Plate is clear** requires **Require plate-clear confirmation** in Bambuddy and a printer
  awaiting clearance. Answer or skip any outcome question first.
- **Hold to start** appears for the next manual-start job assigned to this printer. Bambuddy selects
  a printer for jobs assigned only to a model.
- **How did it print?** appears for a completed print with an unanswered **Ask for outcome**
  request. A skipped or permission-denied prompt is suppressed locally for that run.
- **Filament** appears when the printer reports slots. For missing inventory spools, check the
  material filter, Bambuddy’s built-in inventory and the
  [display limits](using.md#filament-and-maintenance).
- **Printer errors** are filtered. The panel shows actionable or described faults and omits routine
  notices. The printer and Bambuddy can have more detail.

## Firmware updates

**The upload was refused.** Check the installed build name under **Update firmware**, then choose
the matching application image. A full USB image or a different board’s application will be
rejected. An interrupted upload leaves the current boot slot selected.

**The new firmware starts but hangs.** There is no automatic rollback. Follow
[Recover over USB](updating.md#recover-over-usb).

## Reporting a problem

Open a [panel issue or board report](https://github.com/loidolt/bambuddy-panel/issues/new/choose).
Include the board model and profile, firmware version, Bambuddy version, what you did, and what
appeared. Say whether you used hardware or the simulator.

The settings page lists the firmware version. With a USB data cable, `pio device monitor -b 115200`
shows the boot version and periodic memory and stack readings. Remove setup codes, QR codes,
passwords, API keys, server addresses and identifying printer or job names from logs and photos.
Report sensitive issues through the
[panel security policy](https://github.com/loidolt/bambuddy-panel/blob/main/SECURITY.md).
