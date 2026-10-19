-- title: Arm all MIDI tracks
-- description: Arm every MIDI track for recording.

local armed = 0
for _, track in ipairs(emerick.project().tracks) do
    if track.kind == "midi" then
        emerick.run("track.setArm", {track = track.id, value = true})
        armed = armed + 1
    end
end
print("armed " .. armed .. " MIDI tracks")
