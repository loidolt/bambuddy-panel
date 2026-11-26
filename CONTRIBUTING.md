# Contributing

For a bug or board report, include the board profile, firmware revision, Bambuddy version, and whether the result came from the simulator or hardware. The issue forms list the details needed to reproduce a report. Use synthetic printer names and redact setup photos and logs.

Run `pio test -e test` and build the affected board with `pio run -e <profile>`. For UI changes, run `./tools/shots.sh` and update the affected reference images. For documentation changes, install `requirements-docs.txt` and run `mkdocs build --strict`.

The [development guide](docs/development.md) covers the simulator and builds. The [board guide](boards/README.md) explains driver selection and how to report a hardware test. Send security reports through the [security policy](SECURITY.md).
