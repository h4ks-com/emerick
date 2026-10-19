-- title: Rename tracks from a list
-- description: Name the tracks from top to bottom after the list in this script. Edit the list to fit your session.

local names = {"Keys", "Bass", "Drums", "Vocals"}

for index, track in ipairs(emerick.project().tracks) do
    if names[index] then
        emerick.run("track.rename", {track = track.id, name = names[index]})
    end
end
