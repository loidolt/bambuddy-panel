# UX / UI deep-dive plan

Goal: every screen of the panel is polished, readable at a glance, works reliably by touch and
is obvious without a manual, on every supported display. The panel offers what only Bambuddy
can do and leaves the rest to the printer's own screen.

This is the plan for the work, not the work. Section 2 lists what a first read of the code and
the simulator screenshots already turned up; those are leads to confirm, not conclusions.

## 1. What is in scope

**Surfaces**

| Surface | Where | Notes |
|---|---|---|
| Status screen, 9 views | `ui.cpp`, `ui_logic.cpp` | Offline, PrinterOffline, Printing, Paused, Verdict, PlateClear, Error, Ready, Idle |
| Outcome question | `ui.cpp` (`verdictRow`) | Takes over the lower half |
| Maintenance list | `ui_tasks.cpp` | Wrench badge, 4 rows per page, hold-to-Done |
| Message screens | `ui_message` callers in `main.cpp`, `setup_web.cpp`, `calibrate.cpp` | Starting, WiFi setup, Connecting, Setup code, Updating, Offline, Calibration results |
| Touch calibration | `ui_calibrate`, `calibrate.cpp` | Resistive boards only |
| Toast | `ui.cpp` | Command results, "Busy, try again" |
| Status LED | `update_led` | 7 patterns; absent on some boards |
| Setup web pages | `setup_web.cpp`, WiFiManager portal | Phone-first; the only place text is typed |
| README and screenshots | `README.md`, `docs/screenshots` | The first thing a new user sees |

**Displays**

| Screen | Boards | Units (layout grid) | mm per unit | 44-unit target |
|---|---|---|---|---|
| 2.4" 320x240 | ESP32-2432S024R/C | 320x240 | 0.152 | 6.7 mm |
| 2.8" 320x240 | CYD, JC2432W328C | 320x240 | 0.178 | 7.8 mm |
| 3.2" 320x240 | ESP32-2432S032R/C | 320x240 | 0.203 | 8.9 mm |
| 3.5" 480x320 | ESP32-3248S035R/C, WT32-SC01 | 360x240 at 1.33 px/unit | 0.205 | 9.0 mm |

Resistive and capacitive both. The 2.4" resistive board is the worst case for touch; the 3.5"
is the worst case for scaled icons and for unused space. Only the CYD (`cyd-st7789`) is on
hand, so everything else is verified in the simulator and marked as such.

**Out of scope:** portrait layouts, screens above 480x320, multi-printer views, themes.

## 2. Leads from the first read

To confirm or dismiss in phase 2. Grouped by what kind of problem each is.

Feedback and touch
- Hold buttons give text-only feedback ("Keep holding..."); no fill or ring shows how long is
  left, and "OK" stays until the next poll redraws.
- The wrench is 42x30 units: 6.4 x 4.6 mm on the 2.4" board before its 8-unit extended area.
- The maintenance screen closes after 30 s with no warning; "More 1/2" cycles pages one way.
- "Done" in a maintenance row uses its own hold code, separate from the status screen's.

Hierarchy and layout
- "Light ON" is the brightest element on the printing screen and the least important.
- All button labels use the small font in 56-unit buttons; the main action stands out by
  width only.
- On 480x320 the extra 40 units of width are stretched into, not used; when the detail line
  fits on one line an empty band sits above the buttons.
- The state chip is a fixed 120 units; the name gets `W - 192` and long names truncate.
- The toast covers the header, including the state chip.

Information
- No finish time, only time left. A clock time needs NTP or Bambuddy's `Date` header.
- With several errors only the first is shown, cut to two lines with no way to read the rest.
- Preparing shows a progress bar at 0% and a time; the stage name competes with "Next:" for
  the single detail line and loses.
- A single missed poll is silently ignored: nothing tells the reader the data is stale.
- Printer-offline has no action, though Bambuddy can switch a smart plug.

Rendering
- Icons are 1x bitmaps stretched to 1.33x: the wrench and thumbs are visibly soft at 480x320.
- Fonts carry ASCII only, so accented names are flattened and other scripts become `?`.
- The backlight has PWM but nothing dims it; a panel in a bedroom or a dark workshop glows.

Setup
- Not yet reviewed in detail: the first-run sequence (four taps, join AP, pick WiFi, open
  address, type code, URL, key, pick printer) is long and crosses three devices' attention.

## 3. Decisions needed first

These change what gets built, so they come before any polish.

**3.1 Which controls belong on the panel.** Rubric: a control stays if (a) it acts on something
only Bambuddy knows, or (b) a Bambuddy flow at the panel dead-ends without it. Otherwise the
printer's own screen has it.

| Control or info | Today | Printer has it | Proposed |
|---|---|---|---|
| Plate is clear | yes | no | keep |
| Outcome question | yes | no | keep |
| Start next queued job | yes | no | keep |
| Resume held queue | yes | no | keep |
| Maintenance, mark done | yes | no | keep |
| Queue: what is next, how many | yes | no | keep; up to three next jobs where there is room (3.4) |
| Pause / Resume | yes | yes | keep (decided) |
| Stop | yes | yes | keep (decided) |
| Chamber light | yes | yes | **remove** (decided) |
| Clear HMS error | yes | yes (dismiss) | keep (decided): the Error view dead-ends without it |
| Temperatures | shown | yes | Printing and Paused only (decided); the line is free in every other view |
| Power the printer on via its smart plug | no | no | add, later effort (3.4) |
| Power the printer off | no | no | out (3.4) |
| Finish time as a clock time | no | partly | deferred (3.4) |
| Who queued this print | no | no | add, low priority (3.4) |
| Why the next job is not starting | no | no | add (3.4) |
| Cancel / skip the next queued job | no | no | out (3.4) |
| Assign an inventory spool to a slot | no | no (it sets a filament type, not Bambuddy's spool) | added 2026-11-25, built-in inventory; Spoolman later |
| Door open, SD card missing | no | yes | out |
| Speed, temperature targets, jog, AMS load/unload, calibration, skip objects | no | yes | out |
| Cover thumbnail | no | yes | out: no PSRAM, 64 KB LVGL pool |

Decided: Pause, Resume and Stop stay, because a P1S has a small screen with a d-pad where they
are slower to reach than on an X1C or H2D. The chamber light goes. What follows from that:

- `Command::SetLight`, the `LightOn` / `LightOff` looks, `Snapshot::light` and its parsing,
  the request in `bambuddy_api.cpp`, their tests and the simulator scenarios are removed.
- The Idle view has no button unless the queue is held, and PlateClear and Ready have one.
  Phase 4 designs these views for the freed bottom row instead of leaving a gap.
- Printing and Paused have two buttons; Stop no longer needs to be squeezed between two others.
- The README's button table, the API key permission table (Control Printer no longer lists
  light) and the screenshots change with it.

Clear HMS error stays. Temperatures show only while printing or paused (`format_temps` and its
tests change), which with the light gone leaves Idle, Ready, PlateClear and Error most of the
lower screen to redesign in phase 4.

All of section 3's decisions on existing controls are made. The candidates in the table are
judged in phase 1 against permissions, polling budget and space.

Each candidate is checked against the API key permissions it would add, the polling budget
(about 4 s per round on an ESP32 without PSRAM) and whether it fits without a new screen.

**3.4 The candidates, judged.** From reading Bambuddy's routes and schemas (upstream at
7c72a95ed, 2026-11-22); accepted by the owner on 2026-11-25. Phase 1 is closed.

Two of the accepted additions, **power on via smart plug** and **hold the next job**, are
put off as a later effort: both need checks against a running Bambuddy (listed below) that
are not being done now. This round builds the other three: why the next job is not starting,
up to three next jobs, and who queued the print. None needs a permission the panel's
key does not already have.

| Candidate | Verdict | Permission | Requests | Where it goes |
|---|---|---|---|---|
| Why the next job is not starting | **add** | Read Status | none: `waiting_reason` and `filament_short` are already in the queue answer the panel fetches | Detail line in Idle and Ready, in place of or under "Next:" |
| Power on via smart plug | **add, later effort** | Read Status to find the plug, Control Printer to switch it | one `GET /smart-plugs/by-printer/{id}` a minute, as maintenance is read | Hold button in the Printer-offline view, which has none today |
| Up to three next jobs | **add** | Read Status | none: same queue answer, more of it kept | Idle, Ready and PlateClear, in the space the light and the temperatures leave |
| Who queued this print | **add, low priority** | Read Status | none for queued jobs (`created_by_username`, if the queue request also asks for `printing`); reprints would need `GET /printers/{id}/current-print-user` once per print | Next to the job name, only when Bambuddy sends a name |
| Finish time as a clock time | **defer** | none | none: the `Date` header gives UTC | Needs the panel's time zone with daylight saving, which Bambuddy's API does not give and a browser cannot hand over in the form the ESP32 wants |
| Power off | **out** | | | Cuts power to a hot nozzle; Bambuddy's Auto Power Off does it after cool-down |
| Hold the next job | **add, later effort** | Manage Queue, as Start | none to show it; one `PATCH /queue/{id}` with `{"manual_start": true}` to do it | Third button while printing (the light's old place); the job then shows as Ready and the existing "Hold to start" releases it |
| Cancel the next job | **out** | | | Editing the queue belongs where the whole queue is visible; the panel sees one job and its owner may be someone else |

To verify before building (the first two bullets belong to the later effort):

- Hold the next job: Bambuddy has no switch that pauses a printer's whole queue. What it has
  is the per-job "wait for manual start" flag, which the panel's Start button already clears.
  Two things to check on a real Bambuddy: (1) the scheduler skips a staged job and goes on, so
  with several jobs waiting the one behind may start instead; if so the panel stages every
  pending job assigned to this printer (`PATCH /queue/bulk`) or the button is only offered
  when one job waits; (2) a job already being sent to the printer answers 409 and needs a
  toast that says so. Only for jobs assigned to this printer, never "any X1C" jobs, and with
  the same owner rules as Start. Where *Require plate clear* is on, not confirming the plate
  already holds the queue, and the button mostly matters where it is off.

- Smart plug: that `last_state` in the by-printer answer is fresh enough to decide whether to
  offer "power on", or whether `GET /smart-plugs/{id}/status` is needed (it asks the plug
  itself and may be slow). MQTT plugs are monitor-only and get no button. A plug whose
  `controls_printer_power` is off is not offered.
- Waiting reason: Bambuddy's own wording ("Waiting for filament: ...") can be long; it gets the
  two-line detail area and a scenario at full length.
- Three next jobs: 2 more names in the Snapshot (about 150 bytes) and what that does to the
  queue request's JSON filter and the 64 KB LVGL pool, measured in the simulator.

The README's "What it does" table and API key table change when each of these is built, not
before: the README describes the firmware as it is.

**3.2 Display set.** Decided: the four sizes above are the whole target. Landscape only, two
layout grids (320x240 and 360x240 units). So the unit scaling stays, and 480x320 gets at most
one alternative arrangement per screen where the extra 40 units of width earn it; no general
breakpoint system, no portrait.

**3.3 Text coverage.** Decided: Latin-1. What follows from that:

- LVGL's built-in Montserrat sizes are replaced by fonts generated from the Montserrat TTF
  with `lv_font_conv`: U+0020-007E and U+00A0-00FF, plus the LVGL symbols the UI uses (the
  Back arrow). Six sizes: 14, 20, 28 for 320x240 and 18, 26, 36 for 480x320. A script in
  `tools/` regenerates them and CI checks they are up to date, as it does for the icons.
- Each board still builds in only its own three sizes. The flash cost per board is measured
  against the partition table before the fonts are committed.
- The name clean-up (`copy_ascii`) keeps Latin-1 and still flattens or replaces everything
  outside it; its tests and the "printing-accents" scenario change to match.
- Montserrat's licence (OFL) and the generated files go into `THIRD_PARTY.md`.
- The README line about plain ASCII is rewritten.

## 4. Phases

### Phase 0: make everything visible

Nothing is judged from memory; every state at every size is on one page.

Done, except where noted. `tools/shots.sh all` writes `shots/index.html`: 98 screens at each of
320x240 and 480x320, the touch-target pictures, the checks' findings and the settings page.
First run of the checks: at 320x240 every button on the maintenance screen is 6.7 mm tall on a
2.4" panel, and long names and errors are cut with dots; at 480x320 only long names are cut.

Not done: the checks do not fail the build yet (phase 7), nothing looks for overlapping text
that LVGL did not shorten beyond a pairwise box test, and the WiFi page of the setup network
(WiFiManager's own) can only be seen on a real panel.

- Extend `tools/shots.sh` to render all scenarios at 320x240 and 480x320 and build one contact
  sheet per size, plus a side-by-side sheet per scenario.
- Add the scenarios the simulator lacks: hold in progress and just fired, every toast text from
  `command_result`, several errors, maintenance with 0 / unknown / 1 / 12 tasks and page 2,
  every `ui_message` screen with its real text, calibration steps and failure, 100 h print,
  0 layers, longest name in every field at once, key lacking each permission.
- Add a sim flag that outlines every clickable object with its size in mm for a given diagonal,
  so touch targets are measured, not estimated.
- Add a sim check that reports any label whose text was clipped or dotted.
- Screenshot the setup web pages at phone and desktop widths, light and dark.

### Phase 1: scope decisions

Settle section 3 with the owner. Write the outcome into the README's "What it does" table so
the rule is public. Everything after this phase assumes the control set is fixed.

### Phase 2: screen-by-screen audit

Each surface in section 1, at each size, against one checklist. Every finding gets a severity
(blocks use / misleads / slows / cosmetic), a screenshot and the size it shows on.

Done from the simulator: [ux-findings.md](ux-findings.md) has 1 blocking, 12 misleading, 16
slowing and 8 cosmetic findings. The parts of the checklist that need a real panel (glance
from 2 m, the LED) wait for phase 6.

- Glance: state readable from 2 m in under a second; one focal point per view.
- Hierarchy: primary action obvious; destructive action distinct by more than colour.
- Text: no clipping in any scenario; consistent wording, tense and capitalisation across
  buttons, chips, headlines, toasts and message screens.
- Touch: every target at least 7 mm on the 2.4" board including its extended area, 2 mm
  between neighbours; nothing destructive next to something frequent without a hold.
- Feedback: every press shows something within 100 ms; every command ends in a visible result;
  holds show progress and cancel cleanly.
- States: loading, empty, stale, error and "not allowed" each have a designed look.
- Colour: contrast measured on the RGB565 values the panel really shows; views distinguishable
  with red-green colour blindness; meaning never carried by colour alone.
- Space: at 480x320 nothing is merely stretched; no dead bands.
- LED: each pattern maps to one meaning and matches the screen; boards without an LED lose
  nothing.

### Phase 3: flow audit

Walk each journey end to end, counting steps, device switches and places to get stuck.

Done from the code and the simulator: 17 findings, F1 to F17, in
[ux-findings.md](ux-findings.md). Two block (no way into settings from a running panel, no
way out of them once in). What the WiFi page does on a wrong password is to confirm on a
real panel.

- First run: unbox, calibrate, join AP, choose WiFi, open address, code, URL, key, printer.
- Change settings later; change WiFi; recalibrate (the hold-at-boot gesture is undiscoverable
  after boot).
- Recovery: WiFi down, Bambuddy down, key revoked, key lacks a permission, printer deleted,
  printer powered off.
- One print's life: queued, ready, start, preparing, printing, paused with error, finished,
  outcome, plate clear, next job.
- Maintenance: notice the badge, open, page, mark done, return.
- Setup web page: field order, error wording, keyboard types, behaviour when the code expires,
  password-manager behaviour for the key field.

### Phase 4: design system

Turn the findings into shared parts so the fixes stay consistent.

Approved by the owner as drawn on 2026-11-25: [ux-design.md](ux-design.md) has the rules, tokens, components
and each view; [ux-mockups.html](ux-mockups.html) draws 23 screens at both sizes in a browser.
The six points to choose (ux-design.md, section 5) were built as drawn.

- Tokens: colour roles (surface, text, primary, caution, danger, success, toggle), spacing
  steps, radii and the three-step type scale, in one header.
- Components: primary / secondary / destructive / toggle button; one hold button with a
  progress fill, used by the status and maintenance screens alike; chip; list row; toast;
  message screen with optional progress.
- Icons: `tools/make_icons.py` emits each icon at every scale the boards use, so nothing is
  stretched at run time.
- Layout: decide per screen whether 480x320 gets a different arrangement (for example detail
  and queue side by side) or the same one with better spacing.
- Backlight: dim after idle, full on touch or on a state that needs someone; optional night
  level.

### Phase 5: implement in ranked batches

Done in one batch, in the simulator and as firmware that builds for all ten boards; not yet
flashed. What was built, what differs from the design and why is in
[ux-design.md](ux-design.md), section 7; where each finding stands is in
[ux-findings.md](ux-findings.md). Left for a later batch: Latin-1 fonts, and the two
additions that need a running Bambuddy (3.4).

Blocking and misleading findings first, cosmetic last. Each batch: logic in `ui_logic.cpp`
with unit tests, drawing in `ui*.cpp`, before-and-after contact sheets in the pull request.
Web setup changes are a batch of their own.

### Phase 6: on the real board

The simulator does not show contrast, viewing angle, resistive imprecision or redraw speed.
On the CYD: every view photographed straight on and at 45 degrees; each target tapped ten times
with a finger, counting misses, with extra attention to the corners; each hold tried ten
times; redraw flicker during polling; LED visible across a room in daylight; backlight levels
in a dark room. Other boards stay simulator-verified until someone has one; the README says
which.

### Phase 7: keep it that way

- CI renders both sizes and fails on any clipped label or undersized target.
- README screenshots regenerated; `compare_shots.py` extended to the 480x320 set.
- A short `docs/ui-guidelines.md`: the control rubric, the tokens and the checklist, for
  whoever adds a screen or a board.

## 5. Done means

- Zero clipped labels across all scenarios at both sizes.
- Every touch target meets the size rule on the 2.4" board.
- Every command gives feedback within 100 ms and a result within one poll.
- No open finding above "cosmetic"; cosmetic ones listed with a reason if left.
- Every control on the panel passes the rubric in 3.1, and the README states the rule.
- The CYD checks in phase 6 pass; untested boards are named.
- CI guards the first two points.
