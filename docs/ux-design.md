# UI design (phase 4)

What the screens become, and why. Each rule names the findings it answers
([ux-findings.md](ux-findings.md)). Pictures of every view at both sizes are in
[ux-mockups.html](ux-mockups.html); they are drawn in a browser, not by the panel, so type and
spacing are close, not exact. Nothing here is built yet. Points marked **choose** are the
owner's to settle before phase 5.

Sizes are in units (1 px at 320x240, 1.33 px at 480x320).

## 1. Rules

1. **The chip names the state, nothing else.** Printing, Preparing, Paused, Finished, Failed,
   Error, Ready, Idle, Queue held, Offline. It is as wide as its word, so the printer's name
   gets the rest of the header. What the panel wants from the person goes into the headline
   and the button. (S3, S7, M2)
2. **One headline per view, and it is the fact that matters now.** The stage while preparing,
   the job's name when ready to start, the error's own text on an error. Never a restatement
   of the chip. (M8, S6, M4)
3. **Every press is answered at once and stays answered until Bambuddy has spoken.** A pressed
   button shows what is under way ("Stopping..."), the other buttons go quiet, and the screen
   leaves that state only on Bambuddy's answer or a failure. (M1, F8, F6)
4. **Anything that starts a print or cannot be undone is a hold**, and a hold shows its
   progress as a fill moving across the button. Start, plate clear, resume queue, stop, mark
   done. Pause, resume, clear error and skip are taps. (S5, F4)
5. **A button says what will happen, including the consequence.** "Plate is clear" becomes
   "Plate clear, start next" when a job will start on its own. (F3, F5, M7)
6. **Meaning never rests on red against green.** Buttons differ by fill against outline and by
   an icon as well as by colour. Red is for stop, error and failed only. (M12, S14)
7. **Failures look like failures and stay longer.** (M5)
8. **No dead ends.** Every screen that says something is wrong has a button that leads to the
   fix, and every screen entered by a touch has a way back. (B1, F1, F2)
9. **Touch targets are at least 46 units tall**, which is 7 mm on the 2.4" panel. (S1)

## 2. Tokens

Colours, as roles. Values are today's unless marked new; all text pairs stay at 4.5:1 or more
on the 16-bit panel and are re-measured when changed.

| Role | Value | Used for |
|---|---|---|
| surface | `101418` | screen |
| surface-raised | `1B2228` | list rows, the error box's base |
| surface-quiet | `263238` | quiet buttons, the wrench at rest, toast |
| text | `ECEFF1` | |
| text-dim | `90A4AE` | secondary lines |
| go | `2A722E` | Resume, Start, Plate clear, thumbs up, Mark done |
| neutral | `1A62C2` | Pause, Clear error, the Printing chip, progress |
| caution | `9C5700` | Paused chip, Queue held chip, maintenance due |
| danger | `C62828` | Error and Failed chips, thumbs down; Stop's outline and fill |
| danger-text | `FF8A80` | error text, failed headline |
| caution-text | `FFB74D` | waiting reasons, due-soon text |
| go-text | `81C784` | "Print finished" |
| outline | `455A64` | secondary button outline, quiet chip |

Changes from today: amber no longer fills Pause (it was the same brightness as red and
green); maintenance due is caution, not danger; the lit-yellow of the light button goes.

Type: three sizes as now (14, 20, 28 units). Buttons use 20 for a single word or short phrase
and 14 only for a second line. Latin-1 fonts (plan 3.3) carry the symbols the buttons use.

Spacing: 10-unit side margin, 8 between buttons, 6 between text lines, 6 radius. The header
is 40 units; the button row is 56 and sits 6 above the bottom edge in every view. (C8)

## 3. Components

**Button, four kinds**

| Kind | Look | For |
|---|---|---|
| primary | filled (go or neutral), icon + label at 20 | the one thing most people came to do |
| destructive | surface fill, 2-unit danger outline, danger-text label; the hold fill is danger | Stop |
| secondary | surface-quiet fill, text label | Skip, Back, paging, a second action beside a primary |
| pending | the pressed button at 60% with "...ing" text; the others at 40% and not pressable | from press until Bambuddy answers |

Icons from the symbol font: pause, play, stop, check, warning, left, right, power, gear.

**Hold**: a fill grows from the left edge over 1.5 s under the label, which does not change.
Letting go early drains it. At the end the button goes to pending. One implementation for the
status screen and the maintenance list.

**Chip**: content width plus 10 padding each side, 24 tall, state colour.

**Notice** (replaces the toast): a full-width band over the header row, 40 tall. Plain for
success, 2.5 s. Danger band with a warning icon for failures, 6 s, a tap dismisses it. It
covers the whole header on purpose, not part of it. (S4, M5)

**Error box**: surface-raised with a danger left edge, warning icon, up to three lines of the
error's text, "1 of 3" at the right when there are more; a tap shows the next. (M4, S12)

**Wrench**: 42x30 with its 8-unit extended area as now. Caution fill with a count when tasks
are due, outline with a count when due soon, quiet when fine. (S14)

**Link mark**: when the last answer from Bambuddy is more than one poll old, the chip dims
and a small clock sits beside it. (M11)

**Message screen**: title at 20 in the top left, the thing to read or type at 28 in the
middle, a QR code at the right where there is something to open or join, a spinner where the
panel is waiting, buttons along the bottom. (S2, C3, S11, F10)

## 4. Views

| View | Chip | Headline | Below it | Buttons |
|---|---|---|---|---|
| Preparing | Preparing | the stage ("Auto bed leveling") | job name; "about 2h 05m" | Pause, Stop |
| Printing | Printing | 42% and time left, always with "left" | job name above; bar; layer; temperatures; "Next: name (3h 12m), +2 more" on one line | Pause, Stop |
| Printing with an error | Printing | as Printing | error box in place of temperatures and Next | Pause, Stop |
| Paused | Paused | "Paused at 42%" | error box if there is one, else time left and layer | Resume, Stop |
| Outcome | Finished | "How did it print?" | job name | thumbs up, thumbs down, Skip |
| Plate clear | Finished or Failed | "Print finished" / "Failed at 37%" | up to two next jobs (three do not fit under the job line); "starts when the plate is clear" on the first if it will | hold: "Plate clear, start next" or "Plate is clear" |
| Ready | Ready | the job's name, two lines | time; who queued it; the waiting reason in caution-text if Bambuddy gives one | hold: Start |
| Idle, jobs waiting | Idle | "Waiting" | Bambuddy's waiting reason; up to three next jobs | none |
| Idle, nothing | Idle | "Nothing queued" | | none |
| Queue held | Queue held | "2 jobs held" | "after <job> failed" | hold: Resume queue |
| Error | Error | (none; the error box is the headline) | error box, large | Clear error; hold: Resume queue if held |
| Printer offline | Offline | "Printer offline" | "Bambuddy can't reach it. Switched off?" | none (power on: later effort) |
| Bambuddy not answering | last state, dimmed, with the link mark | last screen stays | band: "No answer from Bambuddy for 40 s" | not pressable |
| Settings wrong (401, 403, 404, https, bad address) | | "Check settings" | what Bambuddy said | hold: Open settings |

Bambuddy not answering keeps the last status on screen for five minutes, then gives way to a
message screen with "Open settings". (F9, M6)

**Maintenance**: header 46 (Back, title, a gear, "< 2/3 >"), four rows of 46. Row: name, then
"Overdue by 3 print hours" / "Due in 8 print hours" / "Due in 21 days" / "Due now" (anything
under one hour). Button "Mark done" (hold); after it the row reads "Done" with a check until
the list is read again. The notice names the task. Closes after 60 s untouched. (S1, S9, M7,
C4, C6, F15)

**Settings from the running panel** (B1, F1, F2, F13): the gear on the maintenance screen and
the "Open settings" buttons restart the panel into its settings screen (a flag kept across
the restart; no second web server beside the poller). The settings screen gets "Cancel",
which restarts into normal running, and on a resistive panel "Calibrate touch". The touch at
power-on stays as the fallback for a panel whose touch is too far off to hit a button, and
its prompt is shown only to a panel that is already set up. (F7)

**Setup screens**: the WiFi screen shows a QR code that joins the setup network, with the
name and password beside it at 20. The settings screen shows a QR code for
`http://<address>/?code=<code>`, with address and code beside it; the page fills the code in
from the link. (F10)

**Settings web page** (S15, F12): the panel's printer name in the title and heading once it
has one; one path to the printer list (leaving the key field checks it; no separate button);
"Use these settings anyway" moved away from Save and worded as the exception it is; after a
successful save the form is replaced by the confirmation; colours follow the browser's light
or dark setting.

**Backlight** (S16): full while a view wants someone (Paused, Outcome, Plate clear, Error,
Ready, any message screen) and for 60 s after a touch; otherwise 35%. On a dimmed panel the
first touch only brightens.

**480x320**: the same arrangement on a 360-unit-wide grid; icons drawn at that scale, not
stretched. The extra width goes to names and to the Next line. (C1, C2)

## 5. To choose

1. **Stop as an outline.** It makes Stop quieter than Pause. Alternative: filled red, with
   Pause as an outline. The mockups show the first.
2. **Pause colour.** Neutral blue as drawn, or keep amber and accept it matches red in
   brightness.
3. **"Skip" on the outcome question**, or make "Later" true: ask again after the plate is
   cleared. The second needs the panel to remember across views; small.
4. **Idle with nothing queued**: empty as drawn, or show the last print's result.
5. **Backlight at rest**: 35%, another level, or off entirely after a longer wait.
6. **QR code with the code inside the link.** Anyone who can photograph the screen gets in,
   which is already true of reading it; say if that is not acceptable.

## 6. What this costs

- `LV_USE_QRCODE` and `LV_USE_SPINNER` in the build; flash and LVGL-pool use to be measured
  in the simulator before either is committed to.
- A "pending" state in the UI that the Snapshot does not carry: which command was sent and
  when, cleared by the toast sequence number.
- A restart flag in non-volatile storage for "open settings".
- Three more scenario families in the simulator (pending, link lost, check settings) and the
  hold fill at several points.

## 7. As built

Built as above, with these differences, each forced by something found while building:

- **Bambuddy not answering**: the band and the dead buttons are there; the last status under
  them is not dimmed. Dimming a whole screen makes LVGL draw it into a buffer of its own, and
  the 64 KB it has is not enough for that.
- **A task marked done** says so in its own row ("Saving...", then a check and "Done") and
  not in the band: on the maintenance screen the band would lie over Back and the page buttons.
  A failure still uses the band.
- **Hold fill**: rounded like the button, not clipped to it, for the same memory reason.
- **"Hold to stop"** is in the small size at 320x240, where it does not fit its half of the
  row in the larger one; at 480x320 it does.
- **The QR code** is 92 units, not 104, so the password fits beside it in the larger size.
- **The message, calibration and maintenance screens** are made when shown and taken away
  after. Kept all at once, the new screens ran LVGL out of memory. Now the status screen uses
  about 28 KB and the most in use at once, with the maintenance screen open, is 47 KB of 59 KB.
- **Who queued the print** is shown only when it fits beside the job's whole name.
- **Not built**: Latin-1 fonts (they need a font converter and the font file fetched, which
  was not done in this round), so names still lose their accents. The gap between a missed
  answer and the grey chip is the poll interval, about 4 s.

Section 5's choices were built as drawn: Stop as an outline, Pause in blue, "Skip", an empty
Idle, backlight at 35% at rest, the settings code inside the QR link.

Flash use went from 1,284 KB to 1,329 KB on the CYD (65.3% to 67.6% of the app partition), most
of it the QR code, the spinner and the second set of icons.

## 8. Filament (added after the rest)

Asked for after phase 5: put a spool from Bambuddy's inventory in a slot, from the panel. It
passes the rule in the plan's 3.1: the printer's own screen sets a filament type in a slot,
but knows nothing of Bambuddy's spools.

- **Entry**: a spool icon in the header, before the wrench, only for a printer that reports
  slots. The two header icons are 40x34 with 6 units of reach, 13 apart, so each is 46 tall to
  the touch and their reaches do not overlap.
- **Slots screen**: one row per slot: its name as Bambuddy and the printer have it (A1 to D4,
  HT-A, Ext), the type and colour the printer reports, and the assigned spool with what is
  left on it. A button per row, "Change" or "Assign", a tap.
- **Spool list**: Back names the slot ("< A1") and returns to the slots. A button steps
  through the materials, starting on the slot's own when the inventory has it. Rows: colour,
  brand and colour name, material and grams left; "Assign" is a hold, because it changes
  Bambuddy's records and the printer's slot. The first row, when the slot has a spool, is that
  spool with "Remove" (hold).
- **The answer** shows in the row ("Saving..."), then the slots screen comes back with the new
  spool in it. A failure uses the red band: "API key lacks Manage Inventory".
- **No keyboard, four rows to a page**: with 30 to 150 spools the material step is what makes
  the list usable; a material with 40 spools is still ten pages.
- **Cost**: one more optional permission (Manage Inventory); one request a minute for the
  assignments; the spool list read only when the list is opened, first 150 kept, about 9 KB
  of heap twice; flash 67.6% to 68.4% on the CYD.
- **Not built**: Spoolman (other endpoints); search or sorting by remaining weight; saying
  that a spool is already in a slot of another printer.
