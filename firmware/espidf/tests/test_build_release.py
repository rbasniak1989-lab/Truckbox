"""Packaging regression: sibling sources, actual image identity, and protected offsets."""
import hashlib
import importlib.util
import io
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("tb_package", ROOT / "tools/build_release.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="truckbox release test ")
        self.addCleanup(self.temporary.cleanup)
        self.temp = Path(self.temporary.name)
        self.project = self.temp / "source/firmware/espidf"
        # Copy the real source layout; never use previously built application binaries.
        for name, path in package.source_entries(ROOT).items():
            target = self.temp / "source" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        (self.project / "SOURCE_PROVENANCE.json").write_text(json.dumps({
            "original_release": {"sha256": "test-fixture", "embedded_source_sha256": "test-fixture"}
        }))
        self.output = self.temp / "release"
        self.archive = self.temp / "package.zip"
        for role in ("core", "link"):
            build = self.project / f"build-{role}"
            build.mkdir()
            app = bytearray(512)
            app[0], app[3] = 0xE9, 0x30
            struct.pack_into("<H", app, 12, 13)
            struct.pack_into("<I", app, 32, 0xABCD5432)
            app[48:48 + len(package.VERSION)] = package.VERSION.encode("ascii")
            name = f"truckbox_{role}".encode()
            app[80:80 + len(name)] = name
            app[144:150] = b"v5.5.2"
            (build / f"truckbox_{role}.bin").write_bytes(app)
            for filename in ("bootloader.bin", "partition-table.bin", "ota_data_initial.bin"):
                (build / filename).write_bytes(b"UNIT-TEST FIXTURE ONLY")
            (build / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32c6"\n')
            (build / "project_description.json").write_text(json.dumps({
                "project_name": f"truckbox_{role}", "target": "esp32c6", "project_version": package.VERSION,
                "git_revision": "v5.5.2", "idf_path": str(self.temp / "no-idf-checkout"),
                "c_compiler": "riscv32-esp-elf-gcc",
            }))
            (build / "flasher_args.json").write_text(json.dumps({
                "flash_settings": {"flash_mode": "dio", "flash_size": "8MB", "flash_freq": "80m"},
                "flash_files": {"0x0": "bootloader.bin", "0x8000": "partition-table.bin",
                                "0xf000": "ota_data_initial.bin", "0x20000": f"truckbox_{role}.bin"},
            }))

    def make_package(self):
        return package.package_release(self.project, self.output, self.archive)

    def test_core_only_keeps_link_untouched(self):
        shutil.rmtree(self.project / "build-link")
        result=package.package_release(self.project,self.output,self.archive,roles=("core",))
        self.assertEqual(set(result["images"]),{"core"})
        with zipfile.ZipFile(self.archive) as archive:
            self.assertIn("flash_core.cmd",archive.namelist())
            self.assertNotIn("flash_link.cmd",archive.namelist())

    def test_archive_contains_compilable_sibling_sources_and_integrity_manifest(self):
        result = self.make_package()
        self.assertEqual(result["version"], package.VERSION)
        self.assertEqual(result["sha256"], hashlib.sha256(self.archive.read_bytes()).hexdigest())
        with zipfile.ZipFile(self.archive) as full:
            manifest = json.loads(full.read("manifest.json"))
            self.assertEqual(manifest["state"], "CANDIDATA_DIAGNOSTICO_AGUARDANDO_APROVACAO")
            self.assertEqual(manifest["validation"], "PARTIAL")
            self.assertFalse(manifest["automatic_flash"])
            for role, image in manifest["images"].items():
                self.assertEqual({int(f["offset"], 0) for f in image["usb_files"]}, package.EXPECTED_OFFSETS)
                for item in image["usb_files"]:
                    self.assertEqual(hashlib.sha256(full.read(item["file"])).hexdigest(), item["sha256"])
                self.assertIn(f"flash_{role}.cmd", full.namelist())
            for line in full.read("SHA256SUMS.txt").decode().splitlines():
                sha, filename = line.split("  ", 1)
                self.assertEqual(hashlib.sha256(full.read(filename)).hexdigest(), sha)
            source_bytes = full.read(manifest["source"]["file"])
            self.assertEqual(hashlib.sha256(source_bytes).hexdigest(), manifest["source"]["sha256"])
        with zipfile.ZipFile(io.BytesIO(source_bytes)) as source:
            self.assertIsNone(source.testzip())
            names = source.namelist()
            for required in package.REQUIRED_SOURCE:
                self.assertIn(required, names)
            self.assertFalse(any("/build-" in n or n.endswith((".bin", ".zip", ".elf")) for n in names))
            extracted = self.temp / "extracted"
            source.extractall(extracted)
        sibling = extracted / "firmware/j1708_17pid"
        # Independent compilation of an extracted real module/test catches a source ZIP
        # that merely advertises sibling files without delivering usable code.
        executable = self.temp / "extracted_decoder_test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        str(sibling / "src/tbx_j1587_decoder.c"),
                        str(sibling / "tests/test_tbx_j1587_decoder.c"), "-lm", "-o", str(executable)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run([str(executable)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        cmake_dir = extracted / "firmware/espidf/components/tb_core"
        self.assertTrue((cmake_dir / "../../../j1708_17pid/src/tbx_j1708_requests.c").is_file())

    def test_missing_sibling_fails_before_publishing(self):
        shutil.rmtree(self.project.parent / "j1708_17pid")
        with self.assertRaisesRegex(ValueError, "Required source directory"):
            self.make_package()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.archive.exists())

    def test_cmake_missing_source_fails_before_publishing(self):
        path = self.project / "components/tb_core/CMakeLists.txt"
        path.write_text(path.read_text() + '\nset(test "../../../j1708_17pid/src/missing.c")\n')
        with self.assertRaisesRegex(ValueError, "CMake sibling source absent"):
            self.make_package()
        self.assertFalse(self.archive.exists())

    def test_nvs_region_rejected_before_publishing(self):
        path = self.project / "build-core/flasher_args.json"
        value = json.loads(path.read_text())
        value["flash_files"]["0x9000"] = value["flash_files"].pop("0x8000")
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, "Unexpected flash regions"):
            self.make_package()
        self.assertFalse(self.archive.exists())

    def test_actual_application_idf_version_must_match(self):
        path = self.project / "build-link/truckbox_link.bin"
        data = bytearray(path.read_bytes())
        data[144:150] = b"v5.5.1"
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Expected ESP-IDF v5.5.2"):
            self.make_package()
        self.assertFalse(self.archive.exists())

    def test_actual_application_version_must_match(self):
        path = self.project / "build-core/truckbox_core.bin"
        data = bytearray(path.read_bytes())
        data[48:53] = b"0.1.0"
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Wrong application role/version"):
            self.make_package()
        self.assertFalse(self.archive.exists())

    def test_existing_nonempty_release_is_never_overwritten(self):
        self.output.mkdir()
        original = self.output / "keep.txt"
        original.write_text("preserve")
        with self.assertRaisesRegex(ValueError, "not empty"):
            self.make_package()
        self.assertEqual(original.read_text(), "preserve")


if __name__ == "__main__":
    unittest.main()
