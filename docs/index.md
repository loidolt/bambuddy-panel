# Bambuddy Panel

Bambuddy Panel shows one printer’s status and queue on an ESP32 touchscreen. It connects to your
[Bambuddy server](https://github.com/maziggy/bambuddy) over WiFi so you can pause a print or confirm
the plate is clear while you’re at the printer.

<div class="panel-overview" markdown>
<div markdown>

## Get started

1. [Identify your board](boards.md). The ESP32-2432S028R with the ST7789 display has been tested on
   hardware. The other nine profiles have compile checks only.
2. [Install and set up the panel](get-started.md) with a USB data cable, WiFi and a Bambuddy API
   key.
3. [Learn the controls](using.md) for a running print, a finished plate, a held queue or a spool
   change. Some commands still await testing on a real printer.

You can also [try the desktop simulator](development.md#simulator) without a board.

</div>
<figure>
  <video class="panel-demo" controls playsinline preload="metadata" width="344" height="304" aria-label="Simulator walkthrough of the printer controls, queue, filament and maintenance screens">
    <source src="demo.webm" type="video/webm">
    <source src="demo.mp4" type="video/mp4">
    <a href="demo.mp4">Open the simulator walkthrough (MP4)</a>
  </video>
  <figcaption>A print from queue to plate clearance in the simulator. The dot marks a touch. The light below the display represents the board’s status LED.</figcaption>
</figure>
</div>

## Help and contributions

Bambuddy Panel is an independent community project. For a panel issue, start with
[troubleshooting](troubleshooting.md) or
[file a panel report](https://github.com/loidolt/bambuddy-panel/issues/new/choose). For help with
the Bambuddy server, see the [upstream documentation](https://wiki.bambuddy.cool/).

Panel firmware is available under the
[MIT license](https://github.com/loidolt/bambuddy-panel/blob/main/LICENSE). See the
[development guide](development.md) to build or contribute, and
[third-party notices](https://github.com/loidolt/bambuddy-panel/blob/main/THIRD_PARTY.md) for the
bundled components and documentation stylesheet.
