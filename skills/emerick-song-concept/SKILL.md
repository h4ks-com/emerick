---
name: emerick-song-concept
description: Turn a vague music request into a concrete song brief before any notes are written in Emerick, covering influences, tempo, key, form, an energy curve and an instrument palette the build can actually play. Use this skill at the start of any song, beat, jingle or soundtrack request in Emerick, and whenever a song feels aimless or samey and needs a clearer idea, even if the request only names a genre, a mood or a reference artist.
---

# Song concept

A song that sounds generic usually had no decision behind it. This stage makes the decisions, so the later stages execute a plan instead of improvising one. Write them to `brief.md`, about one screen long, which the other stages read.

## 1. Find the idea in one sentence

Name what the listener should feel and what makes this song not every other song in its genre: "a night drive that keeps accelerating, with a lead that answers itself", "a funk groove where the horns argue with the clavinet". If the request is vague, pick a direction and say so rather than asking many questions.

## 2. Collect influences

List two to four references (genres, eras, artists or tracks) and, for each, the one or two traits worth borrowing: a drum pattern, a chord color, an arrangement trick, a sound. Borrow traits, never melodies. The traits become concrete choices in the next steps.

Useful traits to listen for: tempo and swing, how the drums are built, bass role (root pedal, walking, syncopated), harmonic color (modal, dominant, minor plagal), how sections differ, the signature sound, where the hook sits.

## 3. Set the frame

- **Tempo and feel**: a number, plus straight or swung.
- **Key and mode**: pick the mode for color (dorian for funky minor, mixolydian for bright rock, aeolian for drama, lydian for float). Keep the hook in a comfortable range for its instrument.
- **Meter**: 4/4 unless the idea needs otherwise.
- **Length**: in bars, from the target duration: bars = minutes x BPM / 4 for 4/4. Three minutes at 120 BPM is 90 bars.

## 4. Draw the form and the energy curve

Write the form as sections with bar counts and an energy value from 0 to 1 per section. Energy drives everything later: how many parts play, velocity, drum density, bus level, brightness. Good songs move:

- Start below the middle (0.3 to 0.4) and save the peak (0.9 to 1.0) for the last chorus.
- Make each repeat of a section different: the second verse adds a counter line, the last chorus lifts an octave or adds a harmony.
- Use contrast: a break or bridge that drops energy right before the biggest section makes it land harder.
- Mark transitions: fills into choruses, a one-bar break, a filter sweep in the intro, an ending with a clear final hit.

## 5. Choose a palette the build can play

List the parts (drums, bass, harmony, pad, lead, counter line, effects) and a sound for each. Check what exists before promising it: `plugins.list` shows instruments, the bundled General MIDI instrument lists its sounds in `instrument.params`, and `voices.audition` renders a voice so you can check it is clean (see the arrangement skill). If a style file for the built-in arranger is available, a style can play the whole band; the brief then names the style and which of its sections map to the form.

Give each part a register and a stereo place now, so parts do not fight later: bass low and centered, pad wide in the middle, comping off to one side, lead centered and above the comping.

## brief.md template

```markdown
# <working title>
Idea: <one sentence>
References: <ref>: <traits to borrow>; ...
Tempo: <bpm> <straight|swung>  Key: <key> <mode>  Meter: 4/4  Length: <bars> bars (~<minutes> min)
Form: intro 4 (0.3) | verse 8 (0.5) | chorus 8 (0.8) | ...
Palette: drums <sound>, bass <sound>, ...  (register and pan per part)
Hook: <where it lives and what it does>
Signature moves: <2 to 4 concrete tricks>
```
