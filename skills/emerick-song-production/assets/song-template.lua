-- A complete small song built only from the bundled General MIDI instrument: form, chords, parts, mix, automation
-- and a loudness-normalized render. Copy it, then change the plan tables at the top; the functions below them stay.
-- usage: emerick --script song-template.lua -- <out.wav> [project.erk]
local OUT, FOLDER = arg[1], arg[2]
assert(OUT, "usage: emerick --script song-template.lua -- <out.wav> [project.erk]")

local BPM = 96
-- How far notes on the off-beat eighth lag, in beats: 0 plays straight, about 0.15 swings.
local SWING = 0.12
local PPQ = 960
local BAR = 4 * PPQ
local run = emerick.run

-- Plan -----------------------------------------------------------------------------------------------------------

-- Each section: its chords (one per bar), how hard the band plays (0 to 1), and which parts play.
local FORM = {
    { name = "intro", chords = { "Am9", "Fmaj7", "C6", "G13" }, heat = 0.3, parts = { pad = true, keys = true } },
    { name = "verse", chords = { "Am", "F", "C", "G", "Am", "F", "C", "E" }, heat = 0.5,
        parts = { pad = true, keys = true, bass = true, drums = "verse" } },
    { name = "chorus", chords = { "F", "G", "Am", "C", "F", "G", "E", "E" }, heat = 0.8,
        parts = { pad = true, keys = true, bass = true, drums = "chorus", lead = true } },
    { name = "verse", chords = { "Am", "F", "C", "G", "Am", "F", "C", "E" }, heat = 0.6,
        parts = { keys = true, bass = true, drums = "verse", lead = "answer" } },
    { name = "chorus", chords = { "F", "G", "Am", "C", "F", "G", "E", "E" }, heat = 0.95,
        parts = { pad = true, keys = true, bass = true, drums = "chorus", lead = true } },
    { name = "outro", chords = { "Am", "F", "C", "Am" }, heat = 0.35, parts = { pad = true, keys = true } },
}

-- General MIDI sound names, as the instrument's Sound param lists them.
local SOUNDS = {
    pad = { sound = "Pad 2 (warm)", pan = -0.2, db = -14 },
    keys = { sound = "Electric Piano 1", pan = 0.2, db = -10 },
    bass = { sound = "Electric Bass (finger)", pan = 0, db = -8 },
    lead = { sound = "Lead 2 (sawtooth)", pan = 0.1, db = -13 },
    drums = { kit = "Standard", pan = 0, db = -6 },
}
local PART_ORDER = { "drums", "bass", "keys", "pad", "lead" }

-- The chorus hook, one phrase per bar: {beat, length in beats, pitch}.
local HOOK = {
    { { 0, 1, 69 }, { 1, 0.5, 72 }, { 1.5, 1.5, 76 }, { 3, 1, 74 } },
    { { 0, 2, 74 }, { 2, 1, 72 }, { 3, 1, 71 } },
    { { 0, 1, 72 }, { 1, 0.5, 71 }, { 1.5, 1.5, 69 }, { 3, 1, 67 } },
    { { 0, 3, 67 }, { 3, 1, 64 } },
}

-- Harmony ----------------------------------------------------------------------------------------------------------

local ROOTS = { C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B = 11 }

-- The chord's root as a pitch class and its tones as semitones above the root. It reads names such as C, Am, G7,
-- Cmaj7, Dm9, F6, Bbmaj9, G13, Bm7b5, Bdim, Caug and Gsus4.
local function chordTones(chord)
    local letter, accidental, quality = chord:match("^([A-G])([#b]?)(.*)$")
    local root = (ROOTS[letter] + (accidental == "#" and 1 or accidental == "b" and -1 or 0)) % 12
    local minor = quality:match("^m") and not quality:match("^maj")
    local third = quality:match("sus2") and 2 or quality:match("sus") and 5 or minor and 3 or 4
    local fifth = (quality:match("dim") or quality:match("b5")) and 6 or quality:match("aug") and 8 or 7
    local tones = { 0, third, fifth }
    local extended = quality:match("9") or quality:match("11") or quality:match("13")
    if quality:match("maj") and (quality:match("7") or extended) then
        tones[#tones + 1] = 11
    elseif quality:match("dim7") then
        tones[#tones + 1] = 9
    elseif quality:match("7") or extended then
        tones[#tones + 1] = 10
    elseif quality:match("6") then
        tones[#tones + 1] = 9
    end
    if extended then
        tones[#tones + 1] = 14
    end
    if quality:match("13") then
        tones[#tones + 1] = 21
    end
    return root, tones
end

-- Places a chord close to a center pitch, so voicings move by small steps between chords.
local function voicing(chord, center)
    local root, tones = chordTones(chord)
    local pitches = {}
    for _, interval in ipairs(tones) do
        local pitch = root + interval
        while pitch < center - 6 do pitch = pitch + 12 end
        while pitch > center + 6 do pitch = pitch - 12 end
        pitches[#pitches + 1] = pitch
    end
    table.sort(pitches)
    return pitches
end

-- Tracks and notes -------------------------------------------------------------------------------------------------

local function tick(bar, beat)
    return (bar - 1) * BAR + math.floor((beat or 0) * PPQ + 0.5)
end

math.randomseed(11)
local tracks, clips = {}, {}

local function note(part, bar, beat, length, pitch, velocity, channel)
    if beat % 1 == 0.5 then
        beat = beat + SWING
    end
    run("midi.addNote", {
        clip = clips[part],
        pitch = pitch,
        start = tick(bar, beat),
        length = math.max(1, math.floor(length * PPQ * 0.95)),
        velocity = math.max(1, math.min(127, math.floor(velocity + math.random(-5, 5)))),
        channel = channel,
    })
end

local function velocity(heat, accent)
    return 58 + 50 * heat + (accent and 10 or 0)
end

local PLAYERS = {}

function PLAYERS.pad(bar, chord, heat)
    for _, pitch in ipairs(voicing(chord, 60)) do
        note("pad", bar, 0, 4, pitch, velocity(heat * 0.6))
    end
end

-- The keys comp on beats 1, 2.5 and 4, a little behind the beat for a relaxed feel.
function PLAYERS.keys(bar, chord, heat)
    for _, beat in ipairs({ 0, 1.5, 3 }) do
        for _, pitch in ipairs(voicing(chord, 64)) do
            note("keys", bar, beat + 0.02, beat == 0 and 1.4 or 0.9, pitch, velocity(heat, beat == 0))
        end
    end
end

-- The bass plays the root, and a fifth or an approach note into the next bar.
function PLAYERS.bass(bar, chord, heat)
    local root = chordTones(chord) + 36
    note("bass", bar, 0, 1.5, root, velocity(heat, true))
    note("bass", bar, 1.5, 0.5, root, velocity(heat))
    note("bass", bar, 2, 1, root + 7, velocity(heat))
    note("bass", bar, 3, 0.5, root + 12, velocity(heat * 0.9))
    note("bass", bar, 3.5, 0.5, root + 10, velocity(heat * 0.8))
end

-- General MIDI drums play on channel 10: 36 kick, 38 snare, 42 closed hat, 46 open hat, 49 crash.
local GROOVES = {
    verse = { [36] = { 0, 2.5 }, [38] = { 1, 3 }, [42] = { 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5 } },
    chorus = { [36] = { 0, 1.5, 2, 2.5 }, [38] = { 1, 3 }, [46] = { 0.5, 1.5, 2.5, 3.5 }, [42] = { 0, 1, 2, 3 } },
}

function PLAYERS.drums(bar, _, heat, groove, firstBar)
    for pitch, beats in pairs(GROOVES[groove]) do
        for _, beat in ipairs(beats) do
            note("drums", bar, beat, 0.25, pitch, velocity(heat, beat % 1 == 0) - (pitch == 42 and 15 or 0), 10)
        end
    end
    if firstBar then
        note("drums", bar, 0, 1, 49, velocity(heat, true), 10)
    end
end

function PLAYERS.lead(bar, _, heat, style, _, index)
    local phrase = HOOK[(index - 1) % #HOOK + 1]
    if style == "answer" and index % 2 == 1 then
        return
    end
    for _, n in ipairs(phrase) do
        note("lead", bar, n[1], n[2], n[3] + (style == "answer" and -12 or 0), velocity(heat, n[1] == 0))
    end
end

-- Build ------------------------------------------------------------------------------------------------------------

local lastBar = 1
for _, section in ipairs(FORM) do
    lastBar = lastBar + #section.chords
end

-- instrument.setParam takes the param's id, so we look it up by the name instrument.params lists.
local function setInstrumentParam(track, name, value)
    for _, param in ipairs(run("instrument.params", { track = track }).params) do
        if param.name == name then
            return run("instrument.setParam", { track = track, param = param.id, value = value })
        end
    end
    error("no instrument param " .. name)
end

run("project.new", { path = FOLDER, bpm = BPM })
for _, part in ipairs(PART_ORDER) do
    local spec = SOUNDS[part]
    local track = run("track.addInstrument", { plugin = "org.emerick.gm", name = part }).id
    emerick.waitReady(60)
    if spec.kit then
        setInstrumentParam(track, "Drum kit", spec.kit)
    else
        setInstrumentParam(track, "Sound", spec.sound)
    end
    run("track.setGain", { track = track, db = spec.db })
    run("track.setPan", { track = track, value = spec.pan })
    tracks[part] = track
    clips[part] = run("midi.addClip", { track = track, at = 0, length = tick(lastBar) }).id
end

local bar = 1
local starts = {}
for _, section in ipairs(FORM) do
    starts[#starts + 1] = { name = section.name, bar = bar }
    for index, chord in ipairs(section.chords) do
        for part, style in pairs(section.parts) do
            PLAYERS[part](bar, chord, section.heat, style, index == 1, index)
        end
        bar = bar + 1
    end
end

-- Mix --------------------------------------------------------------------------------------------------------------

local master, masterInserts = emerick.bus("Mix", {
    { compressor = { threshold = -18, ratio = 2, attack = 25, release = 150, makeup = 3 } },
    { eq = { lowCutOn = 1, lowCutFreq = 30 } },
    { limiter = { ceiling = -1, release = 60 } },
})
local room = emerick.bus("Room", { { reverb = { size = 60, decay = 2.2, damping = 45, preDelay = 25, mix = 100 } } })
run("track.setOutputBus", { track = room, bus = master })
for part, track in pairs(tracks) do
    run("track.setOutputBus", { track = track, bus = master })
    if part ~= "bass" and part ~= "drums" then
        run("send.add", { track = track, bus = room, db = part == "pad" and -10 or -16 })
    end
end
local bassEq = run("insert.add", { track = tracks.bass, type = "eq" }).id
run("insert.setParam", { insert = bassEq, param = "lowCutOn", value = 1 })
run("insert.setParam", { insert = bassEq, param = "lowCutFreq", value = 35 })

-- Automation: points are {bar, beat, value, curve?} and beats count from 1, as the transport shows them.
local function sectionBar(name, occurrence)
    local seen = 0
    for _, entry in ipairs(starts) do
        if entry.name == name then
            seen = seen + 1
            if seen == (occurrence or 1) then return entry.bar end
        end
    end
end
emerick.automate(master, "gain", {
    { 1, 1, -4, "hold" }, { sectionBar("verse"), 1, -2, "hold" }, { sectionBar("chorus"), 1, 0, "hold" },
    { sectionBar("verse", 2), 1, -1.5, "hold" }, { sectionBar("chorus", 2), 1, 0.5, "hold" },
    { sectionBar("outro"), 1, -1 }, { lastBar, 1, -12 },
})
emerick.automate(master, masterInserts[2] .. ".lowCutFreq", { { 1, 1, 300 }, { sectionBar("verse"), 1, 30 } })

-- Render -----------------------------------------------------------------------------------------------------------

emerick.waitReady(120)
if FOLDER then
    run("project.save")
end
local mix = run("export.mix", { path = OUT, targetLufs = -14, ceilingDbtp = -1 })
print(string.format("%s: %d bars at %d BPM, %.1f LUFS, %.1f dBTP", OUT, lastBar - 1, BPM, mix.integratedLufs,
    mix.truePeakDbtp))
for track, warning in pairs(mix.warnings or {}) do
    print("warning", track, warning)
end

-- Review: list what a listener would hear as wrong, by track and bar.
local inspected = run("mix.inspect", {})
print(inspected.message)
for _, issue in ipairs(inspected.issues) do
    print("issue", issue.severity, issue.kind, issue.track, issue.from.bbt, issue.detail)
end
