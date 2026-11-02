# Lua patterns for Emerick songs

The patterns `assets/song-template.lua` uses, with the Emerick calls behind them. Read `help("<action id>")` for the exact params of any action.

## The Emerick table

| Call | What it does |
|---|---|
| `emerick.run(id, params)` | runs any action and returns its result table |
| `emerick.batch(calls, keepGoing)` | runs up to 1000 `{id, params}` calls in order, each as its own undo step |
| `emerick.actions(query)` | lists actions matching a query, with descriptions |
| `emerick.project()` | the project state: tracks, clips, buses |
| `emerick.state(name)` | any state entry, such as `transport` |
| `emerick.setting(id, value?)` | reads or sets a setting |
| `emerick.waitReady(seconds?)` | waits until instruments, voices and styles have loaded |
| `emerick.bus(name, inserts)` | adds a bus with inserts and returns its id and the insert ids |
| `emerick.automate(track, param, points)` | adds automation points |
| `emerick.wait(seconds, done?)` | waits inside a `--script` run |
| `help(subject)` | prints the docs of an action id or an `emerick` function |

A `--script` run gets its command line after `--` in `arg`. The Lua sandbox opens only the base, string, table, math and utf8 libraries: there is no `io` or `os`, so write output with `print` and files only through Emerick actions.

## Projects

- `project.new {path = "song.erk", bpm}` refuses a path that already exists, so rerunning a script needs a fresh path or the old project removed first (by the shell that starts the run).
- `project.open {path = "song.erk"}` opens a saved project, for example to inspect a render in a second script.

## Time

Notes and clips take ticks. Keep one helper and never compute ticks by hand elsewhere:

```lua
local PPQ = 960            -- ticks per quarter note
local BAR = 4 * PPQ        -- for 4/4
local function tick(bar, beat) return (bar - 1) * BAR + math.floor((beat or 0) * PPQ + 0.5) end
```

Bars count from 1 and beats inside `tick` count from 0. `emerick.automate` points are `{bar, beat, value, curve?}` with beats counting from 1, as the transport shows them; curve is `linear`, `hold` or `smooth`. This off-by-one between the two is the easiest mistake to make in a song script.

Actions that take a position (`from`, `to`, `at`) accept ticks or a `"bar.beat.tick"` string such as `"9.1.0"`.

Swing: delay every note that starts on the off-beat eighth (`beat % 1 == 0.5`) by a fraction of an eighth, about 0.1 to 0.2 beats for a lazy feel. The template's `SWING` constant does this in one place.

## Tracks, instruments and notes

```lua
local track = run("track.addInstrument", { plugin = "org.emerick.gm", name = "Bass" }).id
emerick.waitReady(60)
local clip = run("midi.addClip", { track = track, at = 0, length = tick(lastBar) }).id
run("midi.addNote", { clip = clip, pitch = 40, start = tick(5, 1.5), length = PPQ // 2, velocity = 96 })
run("midi.addNote", { clip = clip, notes = { { pitch = 40, start = 0, length = PPQ }, { pitch = 43, start = PPQ } } })
```

- `midi.addNote` with `notes` adds the whole list to the clip as one undo step.

- `instrument.setParam` takes the param's numeric id. Look ids up by name from `instrument.params` once (the template's `setInstrumentParam`), so the script survives instruments that reorder their params. Choice params take the choice's text, and `instrument.params` lists every choice: the General MIDI instrument's `Sound` param lists its 128 sounds and `Drum kit` its kits.
- `plugins.list` shows every instrument and effect the build can load, bundled and third party.
- The bundled General MIDI instrument plays drums on MIDI channel 10: pass `channel = 10` to `midi.addNote`, and pick a kit with its `Drum kit` param. Notes on other channels play the melodic `Sound`.
- One long clip per part is simplest: the script decides what plays where, so the clips need no structure.
- Pitch bend and controllers go in as lanes: `midi.setControl {clip, lane = "pitchBend", from, to, points = {{at, value}}}`, with bend values from -8192 to 8191.

## Effects, buses and sends

```lua
local glue, inserts = emerick.bus("Glue", {
    { compressor = { threshold = -18, ratio = 2, attack = 25, release = 150 } },
    { limiter = { ceiling = -1 } },
})
run("track.setOutputBus", { track = track, bus = glue })
run("send.add", { track = track, bus = room, db = -14 })
local eq = run("insert.add", { track = track, type = "eq" }).id
run("insert.setParam", { insert = eq, param = "lowCutFreq", value = 40 })
```

- Built-in effects and their params (`insert.params {insert}` lists any insert's params with ranges, plugin inserts included):

| Effect | Params |
|---|---|
| eq | `lowCut`, `lowShelf`, `peak1` to `peak3`, `highShelf`, `highCut`, each with `On`, `Freq` and `Q`, plus `Gain` for shelves and peaks and `Slope` for cuts, such as `lowCutFreq` |
| compressor | threshold, ratio, attack, release, knee, makeup, autoMakeup |
| delay | time, sync, note, feedback, mix, highCut |
| reverb | size, decay, damping, preDelay, mix |
| limiter | ceiling, release, gain |
| gate | threshold, range, attack, hold, release |
| filter | mode, cutoff, resonance, slope |
| saturation | drive, character, output, mix |
| chorus | rate, depth, delay, feedback, mix |
| utility | gain, width, mono, invertLeft, invertRight, balance |
- Plugin effects go in with `insert.addPlugin` (see `plugins.list`); their params take names or choice text the same way.
- Automate an insert param as `"<insertId>.<param>"`, a send as `"send.<busId>.level"`, and `gain` or `pan` by name.

## Repeatable randomness

Humanize with `math.random`, but call `math.randomseed(<number>)` first, so the same script renders the same song and a change you hear comes from your edit.

## Rendering

```lua
emerick.waitReady(120)
local mix = run("export.mix", { path = OUT, targetLufs = -14, ceilingDbtp = -1 })
print(mix.integratedLufs, mix.truePeakDbtp)
for track, warning in pairs(mix.warnings or {}) do print("warning", track, warning) end
```

Print the loudness and every warning: a warning names a track whose instrument failed to load, a missing file or a voice that decodes badly.

## Measuring

```lua
local inspected = run("mix.inspect", {})
print(inspected.message)
for _, issue in ipairs(inspected.issues) do
    print(issue.severity, issue.kind, issue.track, issue.from.bbt, issue.detail)
end
for _, track in ipairs(run("tracks.analyze", { from = "9.1.0", to = "17.1.0" }).tracks) do
    print(track.name, track.integratedLufs, track.activeRmsDb, track.truePeakDbtp)
end
```

`tracks.analyze` lists buses as well as tracks, and measures them before the export's loudness normalization, so compare tracks with each other and ignore the absolute values.

Two renders of the same script are not byte-identical, so a file hash cannot tell an edit from a rerun. To be sure you review the latest render, check its modification time or print something distinctive from the run.
