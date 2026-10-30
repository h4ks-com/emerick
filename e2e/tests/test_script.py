import os
import subprocess
import unittest

from test_export import HOST_OUT, float_wav_samples

IMAGE = "emerick-dev"
BUILD = """
emerick.run("project.new", {path = "/e2e-out/script-song"})
local track = emerick.run("track.addInstrument", {plugin = "org.emerick.gm", name = "Piano"}).id
local clip = emerick.run("midi.addClip", {track = track, at = 0}).id
for index, pitch in ipairs({60, 64, 67, 72}) do
    emerick.run("midi.addNote", {clip = clip, pitch = pitch, start = (index - 1) * 240, length = 480})
end
emerick.wait(0.1)
emerick.run("export.mix", {path = "/e2e-out/script-mix.wav"})
print("exported", arg[1], #arg)
"""


def run_script(name, code, *args):
    with open(os.path.join(HOST_OUT, name), "w") as file:
        file.write(code)
    return subprocess.run(
        ["docker", "run", "--rm", "-v", f"{HOST_OUT}:/e2e-out", IMAGE, "--script", f"/e2e-out/{name}", "--", *args],
        capture_output=True, text=True, timeout=120)


class Script(unittest.TestCase):
    def setUp(self):
        if not HOST_OUT:
            self.skipTest("EMERICK_E2E_OUT is not set, run e2e/run.sh")

    def test_script_builds_and_exports_a_mix(self):
        result = run_script("build.lua", BUILD, "hello", "3")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "exported\thello\t2\n")
        samples = float_wav_samples(os.path.join(HOST_OUT, "script-mix.wav"))
        self.assertGreater(max(abs(sample) for sample in samples), 0.01)

    def test_script_error_names_the_line(self):
        result = run_script("fail.lua", "print('before')\nerror('stop here')")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "before\n")
        self.assertIn("fail.lua:2: stop here", result.stderr)

    def test_missing_script_is_a_usage_error(self):
        result = subprocess.run(["docker", "run", "--rm", IMAGE, "--script", "/nowhere.lua"],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot read script", result.stderr)


if __name__ == "__main__":
    unittest.main()
