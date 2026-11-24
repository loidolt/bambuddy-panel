// On-device touch calibration: tap four crosses, the result is saved in flash.
#pragma once

// Runs the calibration screen. Blocks until done. Returns false (and keeps the current
// calibration: the saved one when `haveSaved`, else the board's standard values) when nobody
// touches the screen or the taps make no sense.
bool calibrate_run(bool haveSaved);
