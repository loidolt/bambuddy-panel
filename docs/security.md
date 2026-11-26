# Security

The panel is made for a home or workshop network you trust. What that means, piece by piece:

- **API key.** Stored in the panel's flash, which isn't encrypted: anyone who takes the panel
  away can read it. It travels over plain HTTP on your LAN, where anyone on that network can
  read it too, and can then send the panel's commands (stop, pause, start) themselves or show
  the panel a status that isn't true. Give it only the [permissions it needs](get-started.md#set-up-a-panel), limit it to one printer, and delete it in
  Bambuddy if a panel goes missing.
- **Setup network.** Open only while you set up the WiFi; it closes after 10 minutes without
  use. It has a password of twelve random letters and digits, new every time and shown only on
  the panel's screen. The extra pages of the setup library (firmware upload, erase, restart,
  device info) are switched off.
- **Settings page.** Served on your network only during setup or after a touch at start-up,
  and closed again after 10 minutes without a Save once the panel is set up. Every change
  needs the code shown on the panel's screen (the QR code there carries it too), so only
  someone who can see the panel can make one; a wrong code costs a second, and after ten the panel shows a new one. The page is plain
  HTTP, so what's typed into it crosses your network as readable as the panel's own requests.
  It never shows the saved API key, and the key is only kept for the Bambuddy URL it was saved
  with: change the URL and it must be entered again, so nobody at the panel can have it sent
  to a server of their own.
- **Firmware update.** The settings page takes a new firmware file, so it is open only when
  that page is, and needs the same code: whoever can see the panel's screen can put other
  firmware on it. The file is not signed. The panel checks that it is an application built for
  this board, which guards against a wrong file, not against a tampered one; download it from
  the releases yourself and compare it with `SHA256SUMS.txt` if in doubt. The upload is plain
  HTTP like the rest of the page. There is no automatic way back: firmware that starts but
  hangs needs USB to replace.
- **Pushed updates over WiFi** (ArduinoOTA) are off unless you build them in with a password
  ([Development](development.md#pushed-updates-over-wifi)).
