# Tracker Player

Tracker Player is a lightweight Windows desktop tracker module player built with C, `libopenmpt`, and `PortAudio`. It plays module formats such as `.xm` from a simple executable and is configured as a Visual Studio project.

## Features

- Plays tracker module files through `libopenmpt`
- Audio output powered by `PortAudio`
- Single-instance behavior
- Can stop an already running instance through a Windows message window
- Supports Win32 and x64 Visual Studio builds
- Loops playback continuously

## Project Structure

- `Main.c` — application entry point and playback logic
- `TrackerPlayer.vcxproj` — Visual Studio project file
- `Manifest.xml` — Windows common controls manifest
- `Resources.rc` — Windows resource script
- `ThirdParty/Include/` — bundled headers for third-party libraries
- `ThirdParty/Lib/` — bundled import/static libraries for `x86` and `x64`
- `FunkyStars.xm` — sample tracker module file

## Usage

Run the executable with a module file path as the first command-line argument.

Example:

`TrackerPlayer.exe FunkyStars.xm`

Behavior summary:

- If no file path is provided, the application shows an error message.
- If another instance is already running and no file path is passed, the running instance is asked to stop.
- If another instance is already running and a file path is passed, the application refuses to start a second playback instance.
- When playback reaches the end of the module, it restarts from the beginning.

## Implementation Notes

- The application uses `wWinMain` as a Windows GUI entry point.
- Module files are loaded entirely into memory before creating the `libopenmpt` module handle.
- Audio playback uses a default stereo output stream at `48000 Hz`.
- The buffer size constant in the source is `480` frames.
- The program tries non-interleaved float output first and falls back to interleaved float output if needed.

## License

This project is licensed under the MIT License. See [LICENSE.md](LICENSE.md) for details.
