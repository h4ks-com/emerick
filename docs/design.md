# Emerick design

The player's hands are often on the instrument, so the screen reads at a glance from a meter away, and every action is reachable by name through Cmd+K or an agent.

## Tokens

| Token | Hex | Use |
|---|---|---|
| `graphite` | `#1B1C20` | window background, tracks lane |
| `slate` | `#25272D` | panels: inspector, headers, control bar |
| `lift` | `#30333A` | raised controls, hovered rows |
| `rule` | `#3B3E46` | hairlines between rows and panels |
| `ink` | `#E6E7EA` | primary text |
| `ink-dim` | `#9599A3` | secondary text, inactive icons |
| `tape` | `#F2B544` | playhead, selection outline, transport counter |
| `rec` | `#EF5350` | record arm, recording clips, record button |

Track colors are a muted eight-color set (teal, blue, violet, rose, amber, green, sand, steel) at 70% saturation, so clips read as color while text on them stays legible. One shape language: 6 px radius on clips and buttons, 10 px on windows and popovers, 0 on lanes and rulers, which are measuring tools.

Glass: the control bar and popovers use a translucent `slate` at 88% with a 1 px top highlight at 6% white, the Mojave vibrancy look. Panels stay opaque, because text over a moving blur tires the eye.

## Type

- System sans (SF Pro on macOS, bundled Inter on Linux) for all UI, sizes 11, 12, 13 and 15 px. Track names 13 px medium, inspector labels 12 px regular `ink-dim`.
- Tabular figures for every number that changes: bars, beats, tempo, dB.
- The transport counter is the one display moment: 28 px, light weight, tabular, in `tape` on `graphite`, like a tape machine's counter. It shows bar, beat, division (a sixteenth) and tick from 1.1.1.1 as Logic does, then tempo and meter, each over an 11 px lowercase `ink-dim` label.

## Layout

```
+----------------------------------------------------------------------+
| ▯ ▤ Name   (glass): ⏮ ▶ ● ■ | 27  3  2  1 | 96  4/4 |      ≋ ⌸ ⌕ ⚙  |
+-----------+----------------------------------------------------------+
| inspector | ruler 1   5   9   13                                     |
|  track    |----------------------------------------------------------|
|  name     | Chord    | Dm7     | G7    | Cmaj7  |  (plugin lane)     |
|  input ▾  | Style    | Main A        |Fill| Main B                    |
|  output ▾ |----------------------------------------------------------|
|  midi in ▾| ● M S  01 Right 1  |▓▓▓▓▓▓▓ clip ▓▓▓▓▓|                   |
|  midi out▾| ● M S  Genos2 L/R  |≈≈≈≈≈ audio clip ≈≈≈≈≈≈|              |
+-----------+----------------------------------------------------------+
```

Left aligned everywhere. Track header holds arm, mute, solo, name and an instrument icon, and a thin level meter along its right edge; the inspector shows the selected track's inputs and outputs per track (audio device and channels, MIDI port and channel), always visible, never hidden in a global preference.

The mixer (X) docks at the bottom of the window above the console. Each track gets a channel strip with its output, a pan knob, a vertical gain fader beside the level meter, mute, solo and arm, and its name on its color. The track header holds the same pan knob beside its gain. Drag a knob to pan, and double-click or Alt-click it to center; its tooltip reads L 23, C or R 40. The pan law (Settings / Audio) sets how much a centered track drops on each side, the same in playback, exports and meters.

A bus mixes the sends and the tracks routed into it and plays on its own output, and its strip follows the track strips. Each strip lists its sends, one level per bus with a pre-fader switch, and its plus button sends to a bus or to a new one. Soloing a track keeps the buses it feeds audible, and soloing a bus keeps the tracks that feed it, as in Logic. Exports render the buses: the mix plays their returns, and each bus becomes its own stem.

The piano roll (P, or double-click a MIDI clip) docks at the bottom above the mixer and edits one MIDI clip on the timeline's zoom and scroll. A keyboard runs down its left with each C named, and pressing a key plays it on the track's MIDI output. Notes are rounded bars colored by velocity from blue to red, and a velocity lane below holds a bar per note to drag. Drag a note to move it, drag its right edge to resize it, drag empty space to select, and Command-click or use the pencil to add a note of the last length. Notes snap to the piano roll grid (Settings / General). While the piano roll has focus, the arrow keys nudge the selected notes by a semitone or a grid step, Shift with Up or Down moves an octave, Delete removes them and Cmd+C, Cmd+V and Cmd+A copy, paste and select notes; Q quantizes. The wheel scrolls the keys, and Alt with the wheel makes the keys taller or shorter.

Automation (A, or the lane button beside the magnet) draws each track's automation lane over its clips as a line with a handle per point in the track color, and the track header swaps its gain row for the automation mode and the lane's parameter: volume, pan, mute or a send level. Click the lane to add a point, drag points or a segment between two points to move them, drag empty space to select, and Alt-click a point or press Delete to remove points. A track in Read plays its lanes and its fader, pan knob and sends follow them; Off ignores them. In Touch, moving the fader, pan knob or a send level during playback writes points until the control is let go, and Latch keeps writing its last value until the transport stops; each pass is one undo step. Exports play the automation.

A marker lane under the bar ruler shows each marker as a named flag, and the inspector lists the markers under the track routes; clicking one moves the playhead there. Drags snap to the beat grid while snap is on, and holding Command (Ctrl on Linux and Windows) snaps them the other way, as Control does in Logic.

An audio clip draws each fade with its shape: linear, equal power, exponential, logarithmic or S-curve. Dragging a fade handle sideways sets the fade's length and dragging it up or down bends its curve, as in Logic. Where clips on a track overlap, the newer clip plays over the older one outside its fades, and "Create crossfade" fades both over the overlap with equal power.

A level meter shows a track's post-fader, post-pan output, or its input while the track is armed or monitoring: one 2 px bar per channel rising from green through yellow to `rec` at full scale, a held peak tick, and a clip light on top that stays lit until clicked or until "Reset clip indicators" runs. Single keys such as Space and X run their actions anywhere except while a text field has focus. Cmd+C, Cmd+X, Cmd+V and Cmd+A edit the focused text, such as a selection in the console log, and edit clips everywhere else.

## Principles

- The timeline is the product. Chrome stays quiet so clips and the playhead carry the color.
- Spend the boldness once: the amber transport counter and playhead. Nothing else glows.
- Every control has a name an agent and Cmd+K can find. No unlabeled icon buttons; icons carry a tooltip that equals their action title.
- Motion only answers the user: a panel slides open, a clip snaps. No ambient animation.
- Copy is plain and imperative: "Record", "Add audio track", "Export stems". Empty states name the action that fills them.
