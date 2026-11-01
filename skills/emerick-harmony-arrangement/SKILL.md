---
name: emerick-harmony-arrangement
description: Write the chord progressions, form and accompanying band parts (drums, bass, comping, pads) of a song in Emerick, and pick and screen the instruments that play them, either as MIDI parts or with the built-in style arranger. Use this skill whenever a song in Emerick needs chords, a groove, a bass line, an arrangement, instruments chosen, or a style loaded, and whenever parts clash, sound muddy or an instrument sounds broken.
---

# Harmony and arrangement

This stage turns the brief's form into chords and a band that plays them. It owns the Plan, Harmony and Players sections of `song.lua` for the accompaniment; the melody stage adds the leads on top. Read `brief.md` first, and the song production skill's `references/lua-patterns.md` for the API.

## Chords

- Write one chord per bar (or two) per section in a table, as names like `Am`, `F`, `G7`, `Cmaj7`, `E`. A table is easy to read, transpose and hand on.
- Give sections different harmony. A verse can circle (i, VI, III, VII), a pre-chorus climb toward the dominant, a chorus land on the relative major or the IV. A borrowed chord (a major V in minor, a bVII in major, a minor iv) gives the moment people remember.
- End sections so the next one feels inevitable: a V or a held IV before the chorus, the tonic only where you want rest.
- Keep the bass on roots at section starts and use passing or approach notes into the next bar for motion.

## Voicings and registers

Masking is the most common reason a mix sounds muddy, and it starts in the arrangement:

- Voice chords close to a center pitch so each chord moves by small steps (the template's `voicing` helper). Big jumps between chords sound like presets.
- Keep thirds out of the low register; below about MIDI 48 use roots and fifths.
- Separate parts by register: bass 28 to 48, comping 52 to 72, pads 55 to 76 spread wide, lead 64 to 88. When two parts share a range, move one an octave or thin it.
- Give each part its own rhythm. A pad holds, the comping plays off-beats, the bass locks with the kick.

## Grooves

- Build drums from a few voices per pattern (kick, snare, hats, one color), and vary density with energy: fewer hats and no crash in verses, open hats and crashes in choruses.
- Put a fill in the last bar before a big section and a crash on its first beat.
- Accent the beats the style leans on (2 and 4 for backbeat styles, the one for funk) and push hats or ghost notes softer.
- General MIDI drums play on channel 10 in Emerick.

## Choosing and screening instruments

A broken or wrong instrument ruins a song that measures fine, so check sounds before building on them:

- `plugins.list` lists the instruments and effects available. The bundled General MIDI instrument always works and loads fast; other instruments may sound better.
- `voices.audition {track, voice, notes}` renders a voice without changing the project and reports per note the harmonic share (near 1 for a clean pitched voice; low means noise or decode artifacts) and the spectral centroid (it should rise with pitch). Reject voices whose harmonic share drops on some notes, or whose centroid jumps around.
- Listen to a few seconds of every new voice, or at least render its notes with `path` and look for odd peaks before the whole song depends on it.

## The built-in style arranger

When a style file is available, `arranger.setup` adds a Chords track, a Style track and one track per style part, and `arranger.style.load` loads the style. The arranger then plays the whole band from the chords and sections you set with `yamaha.chord.set` and `yamaha.section.set` (Intro, Main A to D, Fill, Break, Ending). `arranger.style.list` lists styles with their tempo and sections.

- Inspect what each part plays with `arranger.style.notes` before mixing. Some styles put effect notes (fret noise, slides, hits) on very high keys meant for special guitar voices. On a General MIDI stand-in they sound like high beeps; cap them with the General MIDI instrument's `Highest note` param.
- When a part's own voice sounds wrong, load a different instrument on that part's track; the part keeps playing its notes.
- Write your own MIDI parts on top for anything the style does not do, such as a riff that follows your form.

## Handoff

Leave the Players for the accompaniment working and rendering, the form table final, and a note for the melody stage: where the hook should sit (bars, register) and which bars have space for a counter line.
