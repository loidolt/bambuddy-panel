# Draft entry for the Bambuddy wiki

For <https://wiki.bambuddy.cool/community/>, under **Hardware**, once the panel has run on real
hardware and the repository is public. Written to match the entries already there.

---

### Bambuddy Panel

PlatformIO firmware that turns an inexpensive ESP32-2432S028R ("Cheap Yellow Display", 2.8" resistive
touchscreen, no PSRAM) into a per-printer control panel. One panel sits next to each printer and
shows that printer's state, job, progress, time left and temperatures, with buttons that follow
the state: pause / resume / stop while printing, **plate is clear** when a print finishes so the
queue moves on, start the next staged job, clear an HMS error, toggle the chamber light. When a
print was started with *Ask for outcome*, the panel asks at the printer with a thumbs up / thumbs
down. A wrench in the header opens the printer's maintenance tasks, most urgent first, and marks
them done. Costly actions (stop, plate clear, start, maintenance done) need a 1.5 s hold. The
RGB LED blinks when someone should come over. Setup is a captive WiFi portal; touch is
calibrated on the device. Polls Bambuddy's REST API over WiFi (~4 s cycle) with a per-printer
API key and never talks to the printer or the cloud. Ships a desktop simulator that runs the same
UI code, for working on screens without hardware.

**Author:** loidolt · [Repository](https://github.com/loidolt/bambuddy-panel)
