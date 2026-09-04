# rebulid

`rebulid` is a Qt 6 desktop ground-control application for MAVLink-compatible drones. It includes serial-port communication, mission planning, RTK/NTRIP configuration, telemetry parsing, attitude display, speech feedback, and multi-drone formation controls.

> This is an experimental project. Validate every command in a safe test environment before using it with an aircraft.

## Requirements

- CMake 3.16 or newer
- A C++ compiler supported by Qt 6 (MSVC is used by the existing Windows build)
- Qt 6.5 or newer with these modules:
  - Quick
  - SerialPort
  - TextToSpeech
  - WebEngineQuick

## Build

From the project directory:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable is produced by the CMake target `apprebulid`.

## Project layout

- `Main.qml` — primary Qt Quick interface
- `main.cpp` — application entry point and QML type registration
- `mavlink/` — MAVLink C headers used to decode and create protocol messages
- `*controller*`, `missionplanner.*`, `rtkclient.*` — flight-control, mission, and RTK-related features

## Safety

This software can interact with unmanned aircraft. Keep propellers clear, use a simulator or bench setup first, and retain an independent means of safely taking control.

## License

No project license has been selected yet. Add a license before accepting external contributions or distributing releases.
