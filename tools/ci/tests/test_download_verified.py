import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "download_verified", Path(__file__).resolve().parents[1] / "download_verified.py")
downloader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(downloader)

class Response(io.BytesIO):
    url = "https://downloads.example.test/asset"

class VerifiedDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.destination = Path(self.temporary.name) / "asset.zip"
        self.destination.write_bytes(b"previous verified asset")
        self.payload = b"new verified build input"
        self.entry = {"url": Response.url,
                      "sha256": hashlib.sha256(self.payload).hexdigest(),
                      "size": len(self.payload)}

    def invoke(self, payload, entry=None, response_type=Response):
        with patch.object(downloader, "urlopen", return_value=response_type(payload)):
            downloader.download(entry or self.entry, self.destination)

    def assert_preserved(self):
        self.assertEqual(self.destination.read_bytes(), b"previous verified asset")
        self.assertEqual(list(self.destination.parent.iterdir()), [self.destination])

    def test_matching_input_replaces_destination(self):
        self.invoke(self.payload)
        self.assertEqual(self.destination.read_bytes(), self.payload)

    def test_tampering_does_not_replace_verified_input(self):
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.invoke(b"X" * len(self.payload))
        self.assert_preserved()

    def test_truncated_input_rejected(self):
        with self.assertRaisesRegex(ValueError, "locked size"):
            self.invoke(self.payload[:-1])
        self.assert_preserved()

    def test_oversized_input_rejected(self):
        with self.assertRaisesRegex(ValueError, "locked size"):
            self.invoke(self.payload + b"X")
        self.assert_preserved()

    def test_insecure_redirect_rejected(self):
        class InsecureResponse(Response):
            url = "http://downloads.example.test/asset"
        with self.assertRaisesRegex(ValueError, "insecure"):
            self.invoke(self.payload, response_type=InsecureResponse)
        self.assert_preserved()

if __name__ == "__main__":
    unittest.main()
