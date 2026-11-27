# Security

Use the panel on a trusted local network and place it where people are allowed to operate the
printer. Anyone at the touchscreen can use the controls permitted by its Bambuddy API key. The code
for browser settings protects a different set of actions.

## Bambuddy API key

The panel stores its API key in unencrypted flash and sends it to Bambuddy over plain HTTP. Someone
with physical access to the flash or access to the network traffic could recover the key and use its
permissions. The panel also relies on that network for the integrity of the status it displays.

Create a dedicated key, grant only the actions needed at this printer and restrict its printer
access where possible. Revoke the key in Bambuddy if the panel is lost or the key is exposed. See
[Create an API key](get-started.md#create-an-api-key).

## Setup access

The WiFi setup network uses a new random password each time it opens. The panel displays that
password and a QR code. The portal accepts WiFi settings and times out after ten minutes.

Once on the LAN, the panel opens its settings page during initial setup or when someone
[requests settings](get-started.md#reopen-settings). The current screen code unlocks the page and
authorizes changes. The QR code contains it, so treat photos of either setup screen as credentials.
After ten consecutive incorrect attempts, the panel generates a new code. A configured panel leaves
setup after ten minutes without an authenticated save attempt or firmware upload activity.

The settings page uses HTTP. It hides the saved API key and requires you to enter it again if the
server address changes. **Save** stores the address, key and printer together after a valid status
response. A temporary connection or server failure offers an explicit offline save. **Cancel** on
the panel discards unsaved edits.

## Firmware

Anyone with access to the settings page and its current code can install firmware. The panel checks
the application image’s format, size and board identifier. These checks catch a wrong-board file.
Firmware is unsigned and travels over HTTP. Get release files from the panel repository and
[verify the download](updating.md#verify-a-download).

An interrupted upload leaves the existing boot slot selected. If installed firmware boots but hangs,
[recover over USB](updating.md#recover-over-usb). PlatformIO push updates through ArduinoOTA require
a [custom build with a password](development.md#pushed-updates-over-wifi).

For a suspected vulnerability, use the
[panel project’s private reporting instructions](https://github.com/loidolt/bambuddy-panel/blob/main/SECURITY.md).
