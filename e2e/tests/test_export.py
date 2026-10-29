import array
import os
import struct
import sys
import unittest
import urllib.error

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from mcp import Client

# run.sh mounts this host folder at /e2e-out in the container.
HOST_OUT = os.environ.get("EMERICK_E2E_OUT")
WAVE_FORMAT_IEEE_FLOAT = 3


# The wave module reads only integer PCM, and the mix export writes 32-bit float samples.
def float_wav_samples(path):
    with open(path, "rb") as file:
        data = file.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path} is not a WAV file")
    offset = 12
    audio_format = None
    while offset + 8 <= len(data):
        chunk, size = struct.unpack_from("<4sI", data, offset)
        body = data[offset + 8 : offset + 8 + size]
        if chunk == b"fmt ":
            audio_format = struct.unpack_from("<H", body)[0]
        elif chunk == b"data":
            if audio_format != WAVE_FORMAT_IEEE_FLOAT:
                raise ValueError(f"{path} has format {audio_format}, expected float")
            return array.array("f", body)
        offset += 8 + size + size % 2
    raise ValueError(f"{path} has no data chunk")


class Export(unittest.TestCase):
    def setUp(self):
        if not HOST_OUT:
            self.skipTest("EMERICK_E2E_OUT is not set, run e2e/run.sh")
        self.client = Client(timeout=60)
        try:
            self.client.initialize()
        except (urllib.error.URLError, ConnectionError, TimeoutError, RuntimeError) as error:
            self.skipTest(f"MCP server not answering at {self.client.url}: {error}")

    def test_general_midi_track_exports_audible_mix(self):
        self.client.run("project.new", {"path": "/e2e-out/song"})
        track = self.client.run("track.addInstrument", {"plugin": "org.emerick.gm", "name": "Piano"})["id"]
        clip = self.client.run("midi.addClip", {"track": track, "at": 0})["id"]
        for index, pitch in enumerate([60, 64, 67, 72]):
            self.client.run("midi.addNote", {"clip": clip, "pitch": pitch, "start": index * 240, "length": 480})
        self.client.run("export.mix", {"path": "/e2e-out/mix.wav"})

        samples = float_wav_samples(os.path.join(HOST_OUT, "mix.wav"))
        self.assertGreater(len(samples), 0)
        self.assertGreater(max(abs(sample) for sample in samples), 0.01)


if __name__ == "__main__":
    unittest.main()
