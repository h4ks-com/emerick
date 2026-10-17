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
- The transport counter is the one display moment: 28 px, light weight, tabular, in `tape` on `graphite`, like a tape machine's counter.

## Layout

```
+----------------------------------------------------------------------+
| control bar (glass): ◀◀ ▶ ● ⟲   |  27 . 3 . 2   96   4/4  Cmaj |  ⌘K |
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

Left aligned everywhere. Track header holds arm, mute, solo, name and an instrument icon; the inspector shows the selected track's inputs and outputs per track (audio device and channels, MIDI port and channel), always visible, never hidden in a global preference.

## Principles

- The timeline is the product. Chrome stays quiet so clips and the playhead carry the color.
- Spend the boldness once: the amber transport counter and playhead. Nothing else glows.
- Every control has a name an agent and Cmd+K can find. No unlabeled icon buttons; icons carry a tooltip that equals their action title.
- Motion only answers the user: a panel slides open, a clip snaps. No ambient animation.
- Copy is plain and imperative: "Record", "Add audio track", "Export stems". Empty states name the action that fills them.
