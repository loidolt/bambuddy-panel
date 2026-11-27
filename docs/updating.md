# Updating

Use the panel’s settings page to update over WiFi and keep its WiFi credentials, Bambuddy settings
and touch calibration.

1. Tap the wrench, open **Maintenance** and press **Hold for settings** until the bar fills. Scan
   the QR code, or open the address shown and enter the current code.
2. Find the installed **version** and **build** under **Update firmware**. Choose the `-app.bin` for
   that exact build from a [published release](https://github.com/loidolt/bambuddy-panel/releases).
   For a [source build](development.md#build-and-flash), use `.pio/build/<profile>/firmware.bin`.
3. For a release file, [verify the checksum](#verify-a-download). Under **Firmware file**, choose
   the application image and select **Update firmware**. Leave the panel powered until it restarts.
4. Reopen settings to confirm the version. Local builds use the version label `dev` by default.

The uploader checks the image format, size and board identifier before selecting the new boot slot.
A rejected file or interrupted transfer leaves the current slot selected. If the new firmware starts
but hangs, recover over USB.

## Verify a download

Download `SHA256SUMS.txt` from the same release as the firmware. Replace the filename below with
your downloaded file’s name, then compare the output with the line for that exact name in the
checksum list. The same check works for full and application images.

| Platform | Command |
|---|---|
| Linux | `sha256sum "your-firmware.bin"` |
| macOS | `shasum -a 256 "your-firmware.bin"` |
| Windows PowerShell | `Get-FileHash -Algorithm SHA256 "your-firmware.bin"` |

Use the file only if the hashes match. The firmware is unsigned, so obtain both files from the
project release.

## Recover over USB

**USB recovery with a full image erases WiFi, Bambuddy settings and touch calibration.** Connect a
USB data cable and
[flash the matching full image at address 0x0](get-started.md#flash-the-firmware). The full image
restores the bootloader, partition table, boot selection and application together. Writing only an
application at a fixed offset can leave the other slot selected.

If you have no packaged full image, connect by USB and use
[PlatformIO with the matching board profile](development.md#build-and-flash). Follow the
[setup steps](get-started.md#set-up-a-panel) if the panel prompts for them.
