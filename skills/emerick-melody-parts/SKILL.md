---
name: emerick-melody-parts
description: Write the hooks, lead melodies, counter lines, solos and expressive performance details (velocity, articulation, bends, vibrato) of a song in Emerick as MIDI. Use this skill whenever a song in Emerick needs a melody, a hook, a riff, a solo, a harmony line or call and response, and whenever a part sounds robotic, aimless or too busy.
---

# Melody and parts

This stage writes the parts people sing back: the hook, the leads that answer it, and the solos. It works on top of a rendering accompaniment from the arrangement stage. Read `brief.md` and the arrangement's handoff note first.

## The hook

- Write the hook as a short motif, one to two bars, as a table of `{beat, length in beats, pitch}` per bar. A table is easy to transpose, harmonize, double an octave up or play on another instrument.
- Give it one rhythmic idea and one memorable interval (a leap of a fourth or more, or a repeated note that breaks away), and land long notes on chord tones.
- Repeat it, then vary it: same rhythm with a new ending, same contour a step higher, the answer half of a question-and-answer phrase. Exact repetition four times in a row sounds like a loop.
- Leave space. Rests let the accompaniment and the listener breathe, and make the next phrase hit.

## Counter lines and harmony

- Put counter lines where the hook rests, in a different register, with longer or shorter notes than the hook.
- Harmonize in diatonic thirds or sixths by moving each note a number of scale steps, not semitones, so the harmony stays in key: find the note's degree in the scale table, add the steps, and map back to a pitch.
- Double the hook an octave up on a second sound for the last chorus instead of writing a new line; the lift comes from the doubling.

## Performance

A part sounds programmed when every note is the same:

- Velocity: tie it to the section's energy and accent the first note of a phrase and the downbeats; add a small seeded random spread (about plus or minus 6).
- Articulation: play notes a little shorter than the grid (about 90 to 95 percent of their length); leave legato phrases at full length.
- Timing: pushing comping slightly behind the beat relaxes it; keep the lead on the grid unless the style swings.
- Bends: start the note one or two semitones low and bend up within an eighth of a beat. Write the bend as a pitch bend lane with `midi.setControl`, keep values inside -8192 to 8191, and return to 0 after the note so the next note is in tune. General MIDI sounds bend two semitones at full range.
- Vibrato: on long held notes, add small sine-shaped bend points (about a third of a semitone, four to six cycles per second) after the attack.

## Solos

Build a solo from a small set of named licks (rise, fall, bend, run, long note) in the song's scale, as semitones above a base note, and sequence them with intensity growing across the solo: shorter notes and higher register toward the end, a long note to close. Vary the order so no lick repeats twice in a row.

## Check

Render, then listen for or check: does the hook stand out of the mix in the chorus, do the lead and the comping collide in register, does any long note sit a semitone away from a chord tone, and does the solo build. Hand the mix stage the list of lead parts and where they need to be on top.
