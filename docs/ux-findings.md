# UX findings (phases 2 and 3)

From the simulator's pictures of every screen at 320x240 and 480x320 (`tools/shots.sh all`),
its checks (`shots/checks.txt`), the settings page rendered at phone width, and the colours
measured as the panel's 16-bit display shows them. Nothing here was seen on a real panel:
contrast in daylight, touch accuracy and redraw speed are phase 6. Journeys across screens
(first run, recovery) are phase 3; where one showed up here it is noted.

Severity: **blocks** (a person cannot get on without outside help), **misleads** (the screen
says something that is not so, or hides what matters), **slows** (works, with effort or
doubt), **cosmetic**. "Both" means both sizes. Screenshot names are files in `shots/`.

## Blocks

| # | Finding | Where | Size |
|---|---|---|---|
| B1 | A refused key, a wrong printer id or an https address ends on a screen whose only advice is "Hold the screen at start-up to change the settings". That means pulling the plug. There is no button to open settings from the running panel. | `offline-http-401`, `-403`, `-404`, `offline-err103`, `-err101` | both |

## Misleads

| # | Finding | Where | Size |
|---|---|---|---|
| M1 | After a hold fires, the button reads "OK" and the rest of the screen keeps the old state until the next poll: chip "Printing", Pause still offered, after Stop was accepted. | `pressed-hold-fired`, `maintenance-hold-fired` | both |
| M2 | Queue on hold: the chip says "Idle" under a headline that says the opposite, and the job line shows the last print's name with nothing saying it is the one that failed. | `queue-held` | both |
| M3 | "Waiting to start" with no reason. For a job meant for "any X1C" the panel cannot start it and does not say who will. | `idle-unassigned-next` | both |
| M4 | An error during a print leaves the chip at "Printing"; only one line of red text says otherwise. With several errors the first is shown and nothing says there are more. | `everything-long`, `error-many` | both |
| M5 | A failed command looks like a successful one: same toast, same colour, gone in 2.5 s. "Not allowed (API key)" needs longer than "Paused". | `toast-not-allowed-api-key`, `toast-no-answer-from-bambuddy` | both |
| M6 | "Offline" is the title for things that are not offline: key refused, printer id unknown, https address. | `offline-http-*`, `offline-err103` | both |
| M7 | Maintenance: 0.4 h left reads "Due in 0 h". "h" means print hours and "days" calendar days, and nothing says so. "Done" is an action but reads as a state, more so in green. | `maintenance-tasks-one-1`, `maintenance` | both |
| M8 | Preparing shows an empty bar, "0%", a time left and "Layer 0/203" at full size; the stage ("Auto bed leveling"), the one thing that is true right now, is small and dim, and with a job queued it is not shown at all. | `preparing`, `preparing-queued` | both |
| M9 | From 10 hours on the word "left" is dropped: "99%  26h 05m" could be time spent. | `printing-long-text`, `printing-days` | both |
| M10 | Paused looks like Printing with a different chip colour and one different button. Nothing says why it paused unless there is an error. | `paused` | both |
| M11 | One unanswered poll is ignored on purpose, but nothing marks the picture as possibly old. (From the code, `poller.cpp`; no screenshot can show it.) | status screen | both |
| M12 | Green, red and amber buttons have the same brightness (contrast between them 1.01 to 1.07). With red-green colour blindness Resume, Stop and Pause differ by their words only, and the "Clear plate" chip's red or green carries nothing. | all button rows | both |

## Slows

| # | Finding | Where | Size |
|---|---|---|---|
| S1 | Every button on the maintenance screen (Back, More, Done) is 6.7 mm tall on a 2.4" panel; the rule is 7. All other targets pass, the wrench at 8.8 x 7.0 mm only with its extended area. | `targets/maintenance` | 320x240 |
| S2 | The setup network's password, the panel's address and the code are in the smallest, dimmest text on their screens, under a large title that carries nothing. The network name breaks mid-word ("BambuddyPanel-" / "Setup"). | `msg-03-wifi-setup`, `msg-05-web-first` | both; the break only 320x240 |
| S3 | A long printer name gets 8 characters ("Workshop ...") while the chip beside it is a fixed 120 units, mostly empty. | `printing-long-text` | 320x240 worst |
| S4 | The toast covers the wrench and the chip, and a long one the printer's name too. | `toast-filament-short-see-bambuddy` | both |
| S5 | A hold shows only "Keep holding...", which touches both edges of its button at 320x240. Nothing shows how much longer. | `pressed-hold` | both |
| S6 | Ready: the headline is the generic "Ready to start" and the chip repeats it; the name of the job about to start, the thing to check before holding the button, is small and dim. | `ready` | both |
| S7 | The chip means different things: a state (Printing, Idle, Error), an instruction (Clear plate) or neither (Your call). | header | both |
| S8 | Error with a held queue offers Clear error (amber), Resume queue (blue) and Light with no sign of which comes first or whether the order matters. Ready with a held queue likewise. | `error`, `ready-queue-held` | both |
| S9 | Maintenance pages turn one way only ("More 1/3"), names are cut at about 22 characters at 320x240, and the screen closes after 30 s without warning. | `maintenance-tasks-full-*` | both |
| S10 | Setup is reached by touching the screen in the two seconds after power-on, and calibration by holding four more. Nothing on the running panel says so. (Belongs to phase 3; see B1.) | `msg-01-starting`, `msg-02-setup-hold` | both |
| S11 | "Connecting", "Restarting..." and "Updating" show no movement, so a hang looks the same as progress. "Setup" is the title of four different screens. | `msg-04`, `msg-07` to `msg-11` | both |
| S12 | Error text is cut after two lines with no way to read the rest. | `everything-long`, `error-many` | 320x240 |
| S13 | Printer offline is an empty screen: no reason, no next step. (Power-on is a later effort.) | `printer-offline` | both |
| S14 | Red means Stop, Error, failed, thumbs down and "maintenance due". The wrench badge is the most saturated thing in the header during an ordinary print. | header | both |
| S15 | Settings page: Save and "Use these settings anyway" sit one under the other; the printer is a bare id until "Check key" is pressed; the page does not say which panel it belongs to, which matters with one panel per printer; after a successful save the whole form is still there; no dark mode. | `web/*.html` | phone and desktop |
| S16 | The backlight has a dimmer and nothing uses it. (From the code, `display.cpp`.) | all | both |

## Cosmetic

| # | Finding | Where | Size |
|---|---|---|---|
| C1 | Icons are stretched from their 1x bitmaps: the wrench and both thumbs have stepped edges. | `printing`, `verdict` | 480x320 |
| C2 | The extra 40 units of width are stretched into, not used, and a one-line detail leaves an empty band above the buttons. | `printing`, `ready` | 480x320 |
| C3 | Message screens put everything in the top half. | `msg-*`, `offline-*` | both |
| C4 | "Done" buttons run to the row's edge and lose their right-hand corners. | `maintenance` | both |
| C5 | File names break at an underscore mid-name ("Articulated_dragon_" / "plate_1"). | `printing` | 320x240 |
| C6 | A task with no time and nothing due has an empty second line. | `maintenance-tasks-full-3` | both |
| C7 | Toast wording: "Printer not ready (409)", "Failed (418)", "Nothing to send". | `toast-*` | both |
| C8 | Spacing between header, job, headline, detail and buttons differs from view to view. | status screen | both |

## Flows (phase 3)

Walked through the code (`main.cpp`, `setup_web.cpp`, `calibrate.cpp`, `bambuddy.cpp`,
`poller.cpp`, `ui_logic.cpp`) and the screens above. Timings are the code's own constants;
what Bambuddy does between two polls is inferred from its source and marked "likely".

### First run, resistive panel

| Step | Where the person looks | What they do |
|---|---|---|
| 1 | Panel: "Starting. Touch the screen now for setup." (2 s) | Nothing is needed, but the screen says to touch |
| 2 | Panel: four crosses | Tap each |
| 3 | Panel: network name and a 14-character password | Read them |
| 4 | Phone: WiFi settings | Join the network, type the password |
| 5 | Phone: the page that opens | Choose the home WiFi, type its password |
| 6 | Panel: address and code | Read them |
| 7 | Phone: browser, back on the home WiFi | Type the address, about 13 characters |
| 8 | Bambuddy, in another tab or on a computer | Create a key, untick permissions, copy it |
| 9 | Phone: settings page | Type the code, type Bambuddy's address, paste the key |
| 10 | Phone: settings page | Check key, choose the printer, Save |
| 11 | Panel: "Connecting", then the status screen | |

Three devices' worth of attention, five things typed by hand (two passwords, an address, a
code, Bambuddy's address) and one pasted.

### Findings

| # | Sev. | Finding | Flow |
|---|---|---|---|
| F1 | blocks | Same as B1, seen as a journey: every recovery that needs the settings page (key revoked, printer deleted in Bambuddy, Bambuddy moved to another address, WiFi password changed) starts with unplugging the panel and touching it within two seconds of power. For three of the four the screen only says "Retrying...". | recovery |
| F2 | blocks | A touch in the first two seconds, by accident or because the screen asks for it, opens the settings screen with no way back: on a panel that is set up it stays for 10 minutes without a Save, or until the plug is pulled. Held four seconds on a resistive panel it starts calibration instead, which waits 60 s for a tap. | reconfigure |
| F3 | misleads | "Hold when plate is clear" starts the next print the moment it is accepted, if one is queued for automatic start. The button does not say so; the line above says "Next of 3: ..." without connecting the two. | print lifecycle |
| F4 | misleads | "Resume queue" is a tap. It can start a print just as "Hold to start" does, which needs 1.5 s, and in the Error view it sits beside "Clear error". | print lifecycle |
| F5 | misleads | "Later" on the outcome question means never, on this panel: the question stays away for that print until the panel restarts, and there is no way to bring it back. | print lifecycle |
| F6 | misleads | After "Hold to start" is accepted the job is, likely for a few seconds, pending and no longer manual, which the panel shows as Idle, "Waiting to start". It looks as if the start was undone. | print lifecycle |
| F7 | misleads | "Starting. Touch the screen now for setup." is shown to a new panel too, which is going into setup anyway. A person who does as told lands on "Let go now ... keep holding 4 seconds for calibration" before anything has been explained. | first run |
| F8 | slows | A command shows nothing between the press and the toast, and the toast waits for Bambuddy's answer: usually well under a second, up to 8 s (3 s connect, 5 s read) when Bambuddy is slow, longer if a poll is in flight. A tap button only darkens while pressed. | all commands |
| F9 | slows | Losing Bambuddy for two polls replaces the whole status screen with "Offline". The last known state (which job, how far) is gone exactly when someone comes to look. | recovery |
| F10 | slows | The address and both passwords are typed from the panel's small text. The display can draw a QR code: one for joining the setup network, one for the settings page. | first run |
| F11 | slows | A wrong WiFi password shows on the phone only; the panel keeps showing the same join instructions. (WiFiManager's behaviour; to confirm on a real panel.) | first run |
| F12 | slows | The settings page has two ways to the printer list, "Check key" and Save. Save without a printer answers "Saved. Still needed: the printer" and needs a second Save. | first run |
| F13 | slows | Outside setup the panel's address answers nothing. Someone who bookmarked the settings page gets a browser error, not a line saying how to open it. | reconfigure |
| F14 | slows | Start refused for lack of filament is a 2.5 s toast ("Filament short; see Bambuddy") and then the same Ready screen. (The waiting reason, accepted in 3.4, covers this.) | print lifecycle |
| F15 | slows | "Marked done" does not say which task, and the list re-sorts when Bambuddy answers, so the row that was pressed moves. | maintenance |
| F16 | slows | Pause is a tap next to a hold. Bumping it is recoverable but can mark the print. To weigh in phase 4, not a clear fault. | print lifecycle |
| F17 | cosmetic | The settings screen shows no countdown for its 10 minutes. | reconfigure |

What works well and should survive the redesign: the plate-clear and start holds; the outcome
question coming before the plate; a command acting on the ids that were on screen when the
finger came down; buttons not changing under a finger; the key never leaving for a new address.

## Where each finding stands (after phase 5)

Checked in the simulator at both sizes; nothing has been seen on a real panel yet.

| Stands | Findings |
|---|---|
| Fixed | B1, F1, F2 (settings from the running panel, and a way back out); M1, F8 (a pressed button says what is under way); M2, S7 (the chip is the state); M3, F6, F14 (why the next job waits; "Starting soon"); M4, S12 (error box, count, tap for the next); M5, S4, C7 (failures in a red band for 6 s, plainer words, the band covers the whole header); M6 ("Check settings" apart from "Offline"); M7, C6 (print hours, "Due now", "Mark done", "Not due"); M8 (the stage is the headline); M9 (always "left"); M10 ("Paused at 42%"); M11 (grey chip after a missed answer); S1 (46-unit targets); S2, F10 (QR codes, large password and code); S3 (the name gets the room, smaller type before it is cut); S5 (hold fill); S6 (the job is the headline); S8, F4 (Clear error first; Resume queue a hold); S9 (pages both ways, 60 s); S13 (says what it might be); S14 (maintenance in amber); S15, F12 (settings page); S16 (backlight); F3 (the plate button says when it starts a print); F5 ("Skip"); F7 (no "touch now" on a new panel); F9 (the last status stays); F15 (the answer shows in the task's row); C1 (icons per scale); C4, C8 |
| Partly | M12: buttons now differ by fill against outline and by icon as well as colour, but the Finished and Failed chips still differ by colour and word only. S11: a spinner while joining the WiFi and asking Bambuddy; the restart messages are still static. C2, C3: spacing is even; the lower half of short screens is still empty. |
| Open | S10 (the power-on touch is still the only way in when touch is too far off to press a button; now a fallback, not the main way). F11 (a wrong WiFi password shows on the phone only; to confirm on a panel). F13 (outside setup the panel's address answers nothing). F16 (Pause is a tap). F17 (no countdown on the settings screen). C5 does not arise: file names stay on one line. Accents: Latin-1 fonts not built. |

## Already settled, listed so they are not found twice

- The light button (largest and brightest thing on several screens) is being removed.
- Temperatures outside Printing and Paused are being removed.
- Accents ("Werkstatt Sud") get Latin-1 fonts.

## Passed

- Every text colour on its background is at least 4.9:1 as the 16-bit panel shows it (the
  lowest: white on red 4.91, on amber 4.94). Dim text on the background is 7.3:1.
- No text runs off the screen or over other text in any of the 98 screens at either size.
- Every target on the status screen and the outcome question is 8.5 mm or more on a 2.4" panel.
- Nothing cut at 480x320 except names longer than about 14 characters and file names longer
  than the line.

## Not looked at

- The WiFi page of the setup network (WiFiManager's own): only a real panel serves it.
- The LED: the simulator reports its colour, but whether each pattern reads from across a room
  is phase 6.
