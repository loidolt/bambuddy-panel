# Security

The panel is made for a home or workshop network you trust. What that means, piece by piece:

- **API key.** Stored in the panel's flash, which isn't encrypted: anyone who takes the panel
  away can read it. It travels over plain HTTP on your LAN, where anyone on that network can
  read it too, and can then send the panel's commands (stop, pause, start) themselves or show
  the panel a status that isn't true. Give it only the [permissions it needs](get-started.md#set-up-a-panel), limit it to one printer, and delete it in
  Bambuddy if a panel goes missing.
- **Setup network.** The WiFi setup portal has a 10-minute timeout and may reopen on a later setup attempt. It has a password of twelve random letters and digits, new every time and shown only on
  the panel's screen. The extra pages of the setup library (firmware upload, erase, restart,
  device info) are switched off.
- **Settings page.** Open during initial setup, from the gear in Maintenance, from **Open settings**, or by touch at startup. A configured panel leaves setup after ten minutes without an authenticated Save or firmware transfer. Every change
  needs the code shown on the panel's screen (the QR code there carries it too), so anyone who obtains the current code can make changes; a wrong code costs a second, and after ten the panel shows a new one. The page is plain
  HTTP, so what's typed into it crosses your network as readable as the panel's own requests.
  Opening the page with no code shows only an unlock form. The saved key is hidden, and the key is only kept for the Bambuddy URL it was saved
  with: change the URL and it must be entered again, so nobody at the panel can have it sent
  to a server of their own. Save commits the address, key and printer together after a valid status response. A connection or server failure allows an explicit offline save. Cancel discards pending edits.
- **Firmware update.** The settings page takes a new firmware file, so it is open only when
  that page is, and needs the same code: whoever can see the panel's screen can put other
  firmware on it. The file is not signed. The panel checks that it is an application built for
  this board, which guards against a wrong file, not against a tampered one; download it from
  the releases yourself and compare it with `SHA256SUMS.txt` if in doubt. The upload is plain
  HTTP like the rest of the page. The settings server caps form bodies at 4 KiB and the application file at the spare slot size. A connection has a ten-second request deadline, extended to two minutes for an authorized upload, with a three-second socket read timeout. There is no automatic way back: firmware that starts but
  hangs needs USB to replace.
- **Pushed updates over WiFi** (ArduinoOTA) are off unless you build them in with a password
  ([Development](development.md#pushed-updates-over-wifi)).
