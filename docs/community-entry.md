# Draft entry for the Bambuddy wiki

For <https://wiki.bambuddy.cool/community/>, under **Hardware**, once every button has been
pressed against a real Bambuddy, the repository is public and it has a release. The page that
goes with the card is `docs/wiki/bambuddy-panel.md`. Written to match the entries already there. Drop the
"Early" box once the panel has been in use for a while.

---

### Bambuddy Panel

PlatformIO firmware that turns an inexpensive ESP32-2432S028R ("Cheap Yellow Display", 2.8" resistive
touchscreen, no PSRAM) into a per-printer touch panel. Its relatives with 2.4", 3.2" and 3.5"
screens, resistive or capacitive, and the WT32-SC01 have build profiles too. One panel sits next to each printer and
shows that printer's state, job, progress, time left, layers and temperatures, with buttons that
follow the state: pause / resume / stop while printing, **plate is clear** when Bambuddy waits for
it, start the next staged job, clear an HMS error, resume a queue held after a failed print.
It lists the next jobs and says why the next one is waiting. Where Bambutton and the Bambuddy Keypad are a button or four, this is a
screen: it shows what is going on and asks the *Ask for outcome* question with a thumbs up /
thumbs down before the plate is released. A wrench in the header opens the printer's maintenance
tasks, most urgent first, and marks them done. Anything that starts a print or can't be undone (stop,
plate clear, start, resume queue, maintenance done) needs a 1.5 s hold. The RGB LED blinks when someone should come over. Setup is a
password-protected WiFi portal, joined and opened by QR code from the panel's screen; resistive touch is calibrated on the device. Polls Bambuddy's REST API over
WiFi (~4 s cycle) with an API key and never talks to the printer or the cloud. Ships a desktop
simulator that runs the same UI code, for working on screens without hardware.

!!! warning "Early"

    The author flags the panel as early: expect rough edges. Track the repo for releases.

**Author:** loidolt · [Repository](https://github.com/loidolt/bambuddy-panel)

---

## The pull request

Against [maziggy/bambuddy-wiki](https://github.com/maziggy/bambuddy-wiki), three changes:

1. `docs/wiki/bambuddy-panel.md` from here goes to `docs/community/bambuddy-panel.md`. Its
   relative links (`../features/api-keys.md`, `../reference/api.md`) are written for that place.
2. The card above goes into `docs/community/index.md`, in the Hardware section, in the wiki's
   card markup, with the heading linking to the page:

   ```markdown
   <div class="feature-card" markdown>
   ### [:material-tablet: Bambuddy Panel](bambuddy-panel.md)
   ...the paragraph and the "Early" box...

   **Author:** [loidolt](https://github.com/loidolt) &middot; [Repository](https://github.com/loidolt/bambuddy-panel)
   </div>
   ```
3. A line in `mkdocs.yml` under `Community Add-ons`, after Bambutton:
   `- Bambuddy Panel: community/bambuddy-panel.md`
