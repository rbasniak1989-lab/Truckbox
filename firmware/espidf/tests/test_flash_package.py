import contextlib
import hashlib
import importlib.util
import io
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "tools" / "flash_device.py"
spec = importlib.util.spec_from_file_location("tb_flash", SOURCE)
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)


class FlashPackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="tb package ")
        self.root = Path(self.directory.name)
        (self.root / "tools").mkdir()
        self.app = bytearray(512)
        self.app[0] = 0xE9
        self.app[3] = 0x30
        struct.pack_into("<H", self.app, 12, 13)
        struct.pack_into("<I", self.app, 32, 0xABCD5432)
        self.app[80:80 + len(b"truckbox_core")] = b"truckbox_core"
        self.items = []
        for offset, filename, data in (
            ("0x0", "boot.bin", b"boot"),
            ("0x8000", "partition.bin", b"partition"),
            ("0xf000", "ota_data.bin", b"ota-data"),
            ("0x20000", "app.bin", self.app),
        ):
            path = self.root / filename
            path.write_bytes(data)
            self.items.append({"offset": offset, "file": filename,
                               "sha256": hashlib.sha256(data).hexdigest()})
        self.save_manifest()
        self.file_patch = patch.object(flash, "__file__", str(self.root / "tools" / "flash_device.py"))
        self.version_patch = patch.object(flash.importlib.metadata, "version", return_value="5.6.0")
        self.file_patch.start()
        self.version_patch.start()
        self.addCleanup(self.file_patch.stop)
        self.addCleanup(self.version_patch.stop)
        self.addCleanup(self.directory.cleanup)

    def save_manifest(self):
        manifest = {"version": "test", "images": {"core": {
            "project": "truckbox_core", "flash_size": "8MB",
            "flash_mode": "dio", "flash_freq": "80m", "usb_files": self.items}}}
        (self.root / "manifest.json").write_text(json.dumps(manifest))

    def invoke(self, output="Detected flash size: 8MB\n"):
        completed = subprocess.CompletedProcess([], 0, stdout=output)
        with patch.object(flash.sys, "argv", ["flash_device.py", "--role", "core", "--port", "COM3"]), \
                patch.object(flash.subprocess, "run", return_value=completed) as runner, \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = flash.main()
            return result, runner.call_args_list

    def test_success_preserves_nvs_and_uses_argument_list(self):
        result, calls = self.invoke()
        self.assertEqual(result, 0)
        self.assertEqual(len(calls), 2)
        command = calls[1].args[0]
        self.assertIsInstance(command, list)
        self.assertIn("write-flash", command)
        self.assertNotIn("erase-flash", command)
        self.assertNotIn("0x9000", command)
        self.assertIn(str(self.root / "app.bin"), command)
        self.assertEqual(calls[1].kwargs.get("check"), True)

    def test_wrong_flash_size_never_writes(self):
        result, calls = self.invoke("Detected flash size: 4MB\n")
        self.assertEqual(result, 3)
        self.assertEqual(len(calls), 1)

    def test_corrupt_file_rejected_before_usb(self):
        (self.root / "app.bin").write_bytes(b"broken")
        with self.assertRaisesRegex(ValueError, "alterado"):
            self.invoke()

    def test_wrong_role_binary_rejected_even_with_matching_hash(self):
        self.app[80:112] = bytes(32)
        self.app[80:93] = b"truckbox_link"
        (self.root / "app.bin").write_bytes(self.app)
        self.items[-1]["sha256"] = hashlib.sha256(self.app).hexdigest()
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "incompat"):
            self.invoke()

    def test_manifest_cannot_write_nvs(self):
        self.items[1]["offset"] = "0x9000"
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "regi"):
            self.invoke()

    def test_path_escape_rejected(self):
        self.items[-1]["file"] = "../outside.bin"
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "fora"):
            self.invoke()


if __name__ == "__main__":
    unittest.main()
