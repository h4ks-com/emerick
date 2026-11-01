---
name: emerick-song-production
description: Produce a complete song with the Emerick DAW from a one-line idea to a mixed, loudness-normalized render, driving Emerick over MCP or with a Lua script. Use this skill whenever someone asks an agent to write, compose, arrange, produce, mix or render a song, track, beat, jingle or demo in Emerick, or wants several agents to make music together, even when they only name a genre or a mood ("make me a 3 minute synthwave track").
---

# Song production with Emerick

This skill runs the whole loop the way a small studio does: a producer sets the direction and keeps the song moving through four stages, each with its own skill. One agent can play every role, or each stage can go to its own agent.

| Stage | Skill | Output |
|---|---|---|
| 1. Concept | `emerick-song-concept` | `brief.md`: idea, references, tempo, key, form, energy curve, palette |
| 2. Harmony and arrangement | `emerick-harmony-arrangement` | the form, chords and band parts in `song.lua` |
| 3. Melody and parts | `emerick-melody-parts` | hooks, leads and counter lines in `song.lua` |
| 4. Mix and master | `emerick-mix-master` | buses, inserts, automation, the render and a review |

Then review the render and loop back to the stage that owns each problem. Stop when a review finds nothing the brief cares about, or after three passes; say what is left.

## Work in one script

Write the song as one Lua script, `song.lua`, and render it with `emerick --script song.lua -- <out> [project.erk]`, or send it through MCP with `lua.eval`. A script is the song's single source of truth: rerunning it rebuilds the same project from scratch, so every change is reviewable, nothing drifts, and a bad edit is a one-line revert. Clicking through single MCP actions instead leaves a project nobody can rebuild.

Start from `assets/song-template.lua`. It is a complete small song made only from the bundled General MIDI instrument, and it renders in seconds. Its plan tables at the top (form, sounds, hook) are what the stages change; the helper functions below them rarely need to. `references/lua-patterns.md` explains the patterns the template uses and the Emerick API calls behind them.

Before writing code against an action, read its parameters: `help("midi.addNote")` in Lua, or `actions.list` over MCP. Action names and params change between versions, and guessing costs a failed render.

## The script layout

Keep these sections in this order, so each stage edits its own part and several agents can hand the file to each other:

1. **Plan**: tempo, form (sections with chords, energy and the parts that play), sounds, hooks.
2. **Harmony**: chord parsing and voicing helpers.
3. **Players**: one function per part that writes notes for one bar or one section.
4. **Build**: create the project, tracks and clips, then walk the form and call the players.
5. **Mix**: buses, inserts, sends, faders, pans.
6. **Automation**: the energy curve made audible.
7. **Render**: wait for instruments, save, export, print the loudness and any warnings.

When several agents share a song, only one of them edits `song.lua` at a time. The producer passes it on with a short note of what changed and what the next stage should do.

## Render and review

- Call `emerick.waitReady()` after loading instruments, styles or voices and before rendering, or the render starts before the sounds have loaded.
- Export with `export.mix {path, targetLufs = -14, ceilingDbtp = -1}`; it reports the loudness the file ends at. Pick the format with the path suffix or `format`: wav, flac, ogg, opus (needs a 48000 Hz project) or mp3.
- Review with numbers and with notes. `mix.inspect` lists whines, beeps, clipping, DC and imbalance by track and bar, and `tracks.analyze` measures every track. Numbers cannot tell a broken instrument from a good one, so also check each instrument on its own (see the mix skill).
- After changing the script, confirm you are reviewing the new render (its modification time, or a value the run printed) before saying a problem is fixed. It is easy to listen to a stale file.

## Running on the Emerick service

On the hosted service each project gets a session: one Emerick process with its own MCP endpoint and a folder it may read and write. Create the session, work through its MCP URL, write renders inside the project folder, and close the session when the song is done, so it saves and frees its slot for other agents. Sessions that sit idle are closed for you, but closing yours is faster and keeps your work safe.
