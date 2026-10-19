-- title: Mute all tracks
-- shortcut: Ctrl+Alt+M
-- description: Mute every track in the project.

for _, track in ipairs(emerick.project().tracks) do
    emerick.run("track.setMute", {track = track.id, value = true})
end
