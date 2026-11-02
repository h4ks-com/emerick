---
name: emerick-mix-master
description: Mix, automate, master and review a song in Emerick, from gain staging, buses, sends and inserts to automation, a loudness-normalized export and a measured review of every track. Use this skill whenever a song in Emerick needs mixing, balancing, effects, automation, mastering, exporting or rendering, and whenever a render sounds muddy, harsh, quiet, flat, distorted or has a strange whine, beep or noise.
---

# Mix, master and review

This stage owns the Mix, Automation and Render sections of `song.lua` and the review that closes the production loop. Read the song production skill's `references/lua-patterns.md` for the API.

## Gain staging

- Set balance with track gain first and save inserts for tone. Start with the drums and bass, then add harmony, then leads.
- Instruments differ wildly in level: driven guitars and saturated parts often come out 10 dB hotter than clean ones. Measure instead of guessing: `tracks.analyze {from, to}` over a busy section reports every track's integrated loudness, active RMS and bands, so you can trim parts toward a plan (for example drums and bass loudest, harmony 4 to 8 dB under, lead on top in the chorus). It measures before the export's loudness normalization, so compare tracks with each other, not with absolute targets.
- Use `track.setTrim` for input level and the fader for the balance, so automation on the fader keeps working.

## Buses and effects

- Send every part to a glue bus with a gentle compressor (ratio 2 to 2.5, slow attack so transients pass, 1 to 3 dB of reduction), a low cut around 30 Hz and a limiter at -1 dB as its last insert.
- Put reverb and delay on their own buses at 100 percent wet and feed them with sends (-10 to -22 dB). One shared room makes parts sound like one band; a tempo-synced delay with a high cut adds width without mud. Route the effect buses into the glue bus.
- Cut before boosting: a low cut on everything that has no low end (comping, pads, leads, percussion), a high cut on bright percussion that pokes out.
- Use saturation for grit and presence, chorus or a utility width boost for wide parts, a gate for tight palm mutes or noisy parts. Keep the bass and kick centered and mono.
- Pan with intent: comping and doubles opposite each other, pads wide, lead and bass centered.

## Automation makes the arrangement audible

Static mixes sound flat even when balanced. Map the brief's energy curve onto a few parameters:

- Step the glue bus gain up section by section (a few dB between the intro and the last chorus) with `hold` curves.
- Open a low-pass filter through the intro; thin the low end for a one-bar break, then slam back into the chorus.
- Push a lead's saturation or delay send up at the climax of a solo; widen effect sends in the final chorus.
- Pan sweeps or brightness rises are good for bridges.

## Master and export

Render with `export.mix {path, targetLufs = -14, ceilingDbtp = -1}` for streaming loudness (quieter genres can sit at -16). The result reports the integrated loudness and true peak the file ends at; print them. Use flac or wav for archives and mp3 or ogg for sharing; opus needs a 48000 Hz project.

## Review

Numbers find many problems, but not all of them, so review in layers:

1. Read the export's warnings: a failed instrument, a missing file or a voice that decodes badly shows up there.
2. Run `mix.inspect`. It renders the mix and every track and lists issues by severity with track and bar: whine (a steady narrow tone), beep (a high tone that repeats and holds for at least 40 ms per hit), clipping, DC, imbalance, silent tracks, harsh sections. Fix the source of each, not the symptom: a beep on a guitar part is usually notes the instrument should not play.
3. Check each instrument on its own. A part can measure fine and still sound broken, robotic or wrong. Solo it, export a stem with `export.stems`, or audition its voice, and listen or at least look at its harmonic share and its note range. `mix.inspect` can miss broadband distorted hits, so check the note range of any part that plays sounds it was not written for.
4. Listen to the transitions: intro into verse, every fill, the break before the chorus, the ending.
5. After each fix, re-render and confirm you are reviewing the new file (its modification time, or a value the run printed) before claiming it is fixed.

Report the review as a short list: what changed, the final loudness and true peak, and anything still open, with the stage that should fix it.
