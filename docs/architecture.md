# Emerick architecture

Emerick is a small native DAW in C++23 and Qt 6.11 QML, built to be driven by a person and by an agent through the same actions. The core records and plays audio and MIDI on tracks of clips. Everything specific to an instrument or a workflow is a plugin.

## Layers

Each layer is one CMake target. A layer only depends on the layers above it in this list.

| Target | Qt | Owns |
|---|---|---|
| `emerick_core` | no | units and time (frames, ticks at 960 PPQ, tempo map, positions), strong ids, SPSC queues, fuzzy matcher |
| `emerick_model` | no | the project document: tracks, clips, track I/O, plugin track data, undo commands, JSON load and save |
| `emerick_engine` | no | audio and MIDI backends, transport, playback graph, recording to disk, MIDI capture, offline render and export |
| `emerick_app` | yes | the registry, document and engine controllers, QML models, navigation and reveal, introspection, MCP server, Lua host, settings |
| `emerick_ui` | yes | the QML module `Emerick.Ui`: theme tokens and every view |
| `plugins/*` | both | one folder per plugin; pure C++ parts in their own target with tests |
| `emerick` | yes | the executable: parses flags, wires backends, loads plugins |

The engine never sees the model. `emerick_app` turns the document into an engine `Session` (plain structs) and publishes it to the audio thread. This keeps the engine testable with no project, and the model testable with no audio.

## Single source of truth

- **Document.** All project state lives in `model::Document`. Views, MCP and Lua read it through `emerick_app`; every change is a `model::Command`, so undo, autosave and change notifications have one path.
- **Registry.** Every action, setting, track type, MIDI device profile and plugin QML component is registered once in `app::Registry`. Menus, shortcuts, the Cmd+K palette, MCP tools and Lua all read the registry. A control's QML `objectName` is its registry id.
- **Gestures.** A drag runs its registry actions live through `Registry::updateGesture`, at most once a frame. Each run reverts the last one first, so every view and the engine follow the drag, and `endGesture` leaves one undo step and one action log line with the final values. Escape cancels a drag and reverts it.
- **Notes.** A MIDI clip keeps each note (id, pitch, velocity, start, length, channel) in its notes and every other message in its events. Playback, export and the piano roll read the notes; the engine session gets them as note-ons and note-offs merged with the events, and a recorded take becomes notes when it lands.
- **Automation.** A track keeps one lane of points per parameter id (`gain`, `pan`, `mute`, `send.<busId>.level`) and an automation mode. The app resolves each id to an engine target, and the engine evaluates the lanes every 32 samples and ramps the track's gain, pan and sends between those values.
- **Locations.** Each registry entry can name a UI location (`settings.audio.bufferSize`). Revealing a location changes the `Nav` state object only; QML binds to `Nav`, so the same call works for a click, the palette, an agent and a test.

## Projects

- A project is a bundle: a folder named `<Name>.erk` holding `project.json` and `audio/`. On macOS the app's Info.plist declares it a document package, so Finder shows it as one file and opens it in Emerick.
- A new project starts as `Untitled N.erk` in the `Emerick` folder of the platform music folder, so every take lands in a real folder. Autosave writes `project.json` about 2 s after the last edit or take. Leaving an untitled project that holds nothing deletes it.
- Opening a project checks it for missing audio files, clips that run past their file, audio files at another sample rate than the engine's and routes to missing devices, and offers the issues it finds. `project.health` runs the same check and lists them.
- An action that takes a path, such as `project.open` or `export.logic`, opens a file dialog when the UI runs it without one. An agent passes the path, and a missing one is an error naming the param. An action that deletes files asks first in the UI; an agent passes `confirm: true`.
- `export.logic` writes `<Name> for Logic/`: one numbered WAV per audio track from bar 1 to the project end, and `MIDI.mid` with the tempo, the meter, one named track per MIDI track, the project's markers, and markers from the track types that name the timeline, such as chords and Style sections.

## Engine threading

1. The UI thread builds an immutable `Session` snapshot once per event loop turn that changed the project, and swaps it in by atomic pointer at the next block. Retired snapshots return to the UI thread for deletion.
2. Live changes (mute, arm, transport) go through an SPSC command queue drained at block start.
3. The audio callback runs the graph single threaded; a worker pool joins later behind the same interface when profiling needs it.
4. Armed audio tracks write into per-track rings; a writer thread writes the files. Clip playback reads ahead on a reader thread. The writer thread also keeps each take in progress (its frames, peaks and MIDI so far), which the app polls to draw the take while it records. An audio take skips the round trip latency the devices report, so it lines up with what the player heard, and each track's record delay moves its takes later or earlier on top of that.
5. MIDI arrives with absolute timestamps on the MIDI thread, goes through an SPSC queue, and is placed on the timeline in the callback.
6. The audio thread never allocates, locks or frees.

## Metronome

The engine plays the metronome on the tempo map's beat grid to the sample: a synthesized click, the user's samples, or a drum groove synthesized from kick, snare and hat voices, or it sends the same hits as MIDI notes. Its settings live in the registry's Metronome section, and the controller copies them into the session it publishes. A count-in holds the transport at the record position until its bars end, so the take starts there. Takes record only inputs, and exports build their sessions without the metronome.

## Cycle

The document keeps one cycle region and its on flag, which the ruler's upper half, the transport button and C edit. While the cycle is on and playback crosses its end, the engine jumps to its start inside the block, to the sample: it sends Stop to the clock ports, releases held notes, sends the track types' chase at the start, chases MIDI and sends Start, so a device that ignores Song Position Pointer starts over there. Audio clips play the cycle start from a preload the snapshot builder reads, while their streams read ahead again. Recording plays straight through the end. When an audible track aligns playback, as the Style track does, the cycle starts where that track aligns it, and the transport's start reason says why.

## Inserts

The engine plays each track's insert effects on its mix before the fader. The snapshot builder makes an effect's state once per insert id at the stream's sample rate, with its delay lines at full length, and later snapshots share it, so an edit keeps reverb tails and delay lines and the params glide to their new values. Effects report no latency, and exports play them through the same engine. A param's id is `<insert id>.<param id>`: an automation lane plays it, and `insert.setParam` takes it in place of the insert param.

## Devices

Each track names its own audio input and output (device id plus channels) and MIDI input and output (port id plus channel). The engine opens the devices that track outputs and the metronome route to, and the inputs of armed and monitored tracks only. It opens them while the transport runs or a track listens live (armed, monitored or with a MIDI input), and closes them once stopped, so other apps keep their devices undisturbed. A route to a device that is gone or cannot run at the engine's rate is dropped and reported, and the other routes still play. With no audio route at all, the engine runs on the default output device at the rate and buffer size it already has. On macOS several devices become one hidden private aggregate device with drift compensation, so the user never builds one by hand. On Linux the devices are PipeWire or JACK ports. On Windows each WASAPI device runs its own stream and the engine resamples the others to the clock device.

Listing devices asks CoreAudio and CoreMIDI about every device, so the engine lists them once at startup and keeps the list. It lists them again when the system reports a device appearing or going away, and on `devices.refresh`. A device that cannot run at the engine's sample rate is refused with the rate to set.

## Plugins

A plugin calls `registerWith(Registry&, ProjectController&, Nav&)` and can add track types (data schema, QML header and lane, engine MIDI processor), actions (listed in the menu bar under `Plugins/<plugin name>`), settings and their settings section (listed under the Plugins heading), MIDI device profiles (channel names, icons), project-level data in `Document::pluginData` and a QML module. A lane component gets `trackId`, `trackData` and `pixelsPerTick` from the tracks area. The first plugin, `plugins/yamaha`, adds the Chords and Style tracks, the arranger setup and the voice table for the Genos2. Plugins are compiled in; dynamic loading waits for a third-party plugin.

## CLAP plugins

Emerick hosts CLAP plugins through its own host on clap-helpers. `emerick-clap-scanner`, beside the app, loads one bundle and prints its plugins as JSON. The app runs it once per bundle in the standard CLAP folders, `CLAP_PATH` and the `plugins.clapFolders` setting, and keeps the results in `clap-plugins.json` in the cache folder, keyed by bundle path and modification time. A bundle that crashes, hangs or fails to load stays blocked until it changes or `plugins.rescan` runs. `plugins.list` lists the plugins and the blocked bundles.

## Agent interface

The app serves MCP over streamable HTTP on loopback (`--mcp-port`). Tools: `ui.tree`, `ui.find`, `ui.reveal`, `ui.invoke`, `ui.set`, `actions.list`, `actions.run`, `actions.batch`, `settings.get`, `settings.set`, `state.get`, `lua.eval`, `console.log`, `screenshot`. Every tool returns the state after the change. An action that fails leaves the document and its undo history as they were. `actions.batch` and `emerick.batch` run up to 1000 calls, each as its own undo step. With `--agent-read-root` and `--agent-write-root`, every path an MCP or Lua request names goes through one check that resolves symlinks and `..` and refuses a path outside the root; paths from the person using the UI stay open. Lua (5.4 through sol2) sees the same document and registry, and its `ui` functions and the `ui.*` tools both go through `UiApi`, so a script and an agent can do anything a click can.

## Console and scripts

The console (Cmd+\`) runs Lua in the same sandboxed host on the UI thread. `Registry::run` emits `actionRan` for every action a person, the palette, MCP or Lua asked for, and the console writes each one to its action log as the Lua call that runs it. Lua files in the user scripts folder and in `<bundle>/scripts` become actions in the Scripts menu; a header comment names their title, shortcut and description. tree-sitter parses the Lua the console shows, for its syntax colors.

## Testing

- Catch2 unit tests per target. The engine runs on `OfflineBackend`, faster than realtime, with golden renders.
- `NullAudioBackend` and an in-process virtual MIDI port, selected with `--audio=null --midi=virtual`, let the app run anywhere. With the virtual port, `midi.inject` feeds it and `midi.sent` lists what the app sent to it.
- `docker/` runs the app headless (`QT_QPA_PLATFORM=offscreen`) in Ubuntu 24.04 with Qt 6.11, exposes the MCP port, and `e2e/` drives it over MCP.
- CI runs `make quality` on macOS and Ubuntu and the Docker e2e on every pull request and push to main. An action test fails when a registry action has no menu entry, shortcut or control.

## Libraries

| Use | Library |
|---|---|
| UI | Qt 6.11: Quick, QuickControls2, HttpServer |
| Audio IO | the CoreAudio HAL on macOS, miniaudio elsewhere |
| MIDI IO and MIDI files | libremidi |
| Audio files | libsndfile |
| Queues | moodycamel readerwriterqueue |
| JSON | nlohmann_json |
| Lua | Lua 5.4 and sol2 |
| Syntax highlighting | tree-sitter and tree-sitter-lua |
| Tests | Catch2 v3 |

License: GPL-3.0-or-later.
