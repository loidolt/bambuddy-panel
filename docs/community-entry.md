# Draft entry for the Bambuddy wiki

For <https://wiki.bambuddy.cool/community/>, under **Hardware**, once the panel has run on real
hardware and the repository is public. Written to match the entries already there. Drop the
"Early" box once the panel has been in use for a while.

---

### Bambuddy Panel

PlatformIO firmware that turns an inexpensive ESP32-2432S028R ("Cheap Yellow Display", 2.8" resistive
touchscreen, no PSRAM) into a per-printer touch panel. One panel sits next to each printer and
shows that printer's state, job, progress, time left, layers and temperatures, with buttons that
follow the state: pause / resume / stop while printing, **plate is clear** when Bambuddy waits for
it, start the next staged job, clear an HMS error, resume a queue held after a failed print,
toggle the chamber light. Where Bambutton and the Bambuddy Keypad are a button or four, this is a
screen: it shows what is going on and asks the *Ask for outcome* question with a thumbs up /
thumbs down before the plate is released. A wrench in the header opens the printer's maintenance
tasks, most urgent first, and marks them done. Costly actions (stop, plate clear, start,
maintenance done) need a 1.5 s hold. The RGB LED blinks when someone should come over. Setup is a
password-protected WiFi portal; touch is calibrated on the device. Polls Bambuddy's REST API over
WiFi (~4 s cycle) with an API key and never talks to the printer or the cloud. Ships a desktop
simulator that runs the same UI code, for working on screens without hardware.

!!! warning "Early"

    The author flags the panel as early: expect rough edges. Track the repo for releases.

**Author:** loidolt · [Repository](https://github.com/loidolt/bambuddy-panel)
