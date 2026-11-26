# Troubleshooting

## The picture

**White or garbled on an ESP32-2432S028R.** The board is sold with two display chips. Flash the
other build: `cyd-ili9341` instead of `cyd-st7789`, or the other way round.

**Red and blue swapped, colours like a negative, or upside down.** One flag in the board's
file: see [Boards](boards.md#if-the-picture-or-the-touch-is-wrong).

**The screen went dim.** The backlight dims to about a third after a minute without a touch
while nothing wants anyone. The first touch only brightens it.

**Names lose their accents.** The panel's fonts only have plain ASCII: *Jürgen* becomes
*Jurgen*, other scripts show as `?`.

## The touch lands in the wrong place

On a panel with resistive touch, calibrate it again: open the maintenance screen (the wrench),
hold the **gear**, and choose **Calibrate touch**.

If the touch is too far off to press anything, touch the screen while it says "Starting", just
after power-on, and keep holding for about four seconds: the panel goes to touch calibration.
Letting go when asked for the settings screen opens that instead.

A panel with capacitive touch has nothing to calibrate. If its touch is mirrored or dead, that
is a flag in the board's file: see [Boards](boards.md#if-the-picture-or-the-touch-is-wrong).

## WiFi

**No WiFi at start-up.** A panel that is set up says so and keeps trying; it does not
open its setup network by itself.

**To move it to another WiFi**, open the settings page (the wrench, then hold the **gear**) and
use **Change WiFi**. If the saved WiFi can't be joined once the settings were asked for, the
setup network `BambuddyPanel-Setup` opens instead.

**The setup network is gone.** It closes after 10 minutes without use. Restart the panel.

## Bambuddy

**The chip in the corner is grey.** One request went unanswered: what is on screen may be a few
seconds old.

**A red band says there has been no answer for some time.** Bambuddy hasn't answered twice or
more in a row. The last status stays under the band and nothing can be pressed until it
answers again.

**A screen says why and offers "Open settings".** Bambuddy has refused the API key, the address
can't work, or there has been no answer for five minutes. Check that Bambuddy runs, that the
URL starts with `http://` (https isn't supported), and that the key still exists.

**A red band after pressing a button**, such as "API key lacks Manage Queue": Bambuddy refused
the command and the band says why. The permissions each button needs, and what the key's owner
must be allowed to do, are under [Get started](get-started.md#set-up-a-panel).

## Something isn't offered

**No "Plate is clear" button.** It is offered only when Bambuddy's *Require plate clear*
setting is on.

**No "Start" button for a queued job.** It is offered only for a job assigned to this printer.
A job queued for "any X1C" shows as waiting: Bambuddy picks the printer.

**No outcome question.** It is asked only for prints that completed and were started with *Ask
for outcome*. If the API key may not record the answer, the panel says so once and stops asking.

**No spool in the header.** It shows only for a printer that reports filament slots. Spools
kept in Spoolman are not supported yet.

**An error on the printer doesn't show.** The panel counts what Bambuddy's own printer card
counts as a problem. Notices the printer keeps for a whole print and codes Bambu publishes no
text for are left out.

## Updating

**The panel refuses the firmware file.** It takes only an `-app.bin` built for its own board.
A `-full.bin`, a file for another board, or anything else is refused, and the panel keeps
running what it had. The settings page names the build the panel runs.

**A new firmware starts but doesn't work.** The panel doesn't go back to the old one by
itself. Write a working `-app.bin` over USB: see [Updating](updating.md).

## Reporting a problem

Open an [issue](https://github.com/loidolt/bambuddy-panel/issues/new/choose) with the board and
build, the firmware version and the Bambuddy version. The firmware version is at the foot of the
panel's settings page.

If you can, add the serial log: `pio device monitor` shows the version at start-up (`[boot]
...`) and, once a minute, the version, the free memory and the background task's spare stack
(`[health] ...`). Don't paste your API key.
