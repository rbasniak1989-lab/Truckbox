"""Offline tests for the reviewed backend patch generator; no deployed source required."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

PATCHER = Path(__file__).resolve().parents[1] / "cloud/patch_ingest_v24.py"
SPEC = importlib.util.spec_from_file_location("tb_cloud_patch", PATCHER)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class CloudPatchTests(unittest.TestCase):
    def test_wrong_source_hash_leaves_input_and_output_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original, output, diff = root / "original.ts", root / "review.ts", root / "review.patch"
            original.write_text("unreviewed source\n")
            with self.assertRaisesRegex(ValueError, "SHA256 differs"):
                module.prepare(original, output, diff)
            self.assertEqual(original.read_text(), "unreviewed source\n")
            self.assertFalse(output.exists())
            self.assertFalse(diff.exists())

    def test_input_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / "original.ts"
            original.write_text("keep")
            with self.assertRaisesRegex(ValueError, "must differ"):
                module.prepare(original, original)
            self.assertEqual(original.read_text(), "keep")

    def test_diff_cannot_overwrite_source_or_private_review(self):
        with tempfile.TemporaryDirectory() as directory:
            original, review = Path(directory) / "original.ts", Path(directory) / "review.ts"
            original.write_text("keep")
            for destination in (original, review):
                with self.assertRaisesRegex(ValueError, "Diff must differ"):
                    module.prepare(original, review, destination)
            self.assertEqual(original.read_text(), "keep")
            self.assertFalse(review.exists())

    def test_only_reviewed_fragments_change(self):
        sentinel = "// unrelated authentication and persistence code remains unchanged\n"
        source = sentinel + "\n".join(old for old, _ in module.REPLACEMENTS) + sentinel
        changed = module.patched_text(source)
        self.assertTrue(changed.startswith(sentinel))
        self.assertTrue(changed.endswith(sentinel))
        for _, replacement in module.REPLACEMENTS:
            self.assertIn(replacement, changed)
        self.assertIn("rows.filter(isOperationalRow).reduce", changed)
        self.assertIn("s.time_valid===true", changed)

    def test_missing_or_duplicate_fragments_rejected(self):
        fixture = "\n".join(old for old, _ in module.REPLACEMENTS)
        for source in ("unknown source", fixture + module.REPLACEMENTS[0][0]):
            with self.assertRaisesRegex(ValueError, "missing or duplicated"):
                module.patched_text(source)

    def test_public_diff_contains_no_authentication_block(self):
        delta = (PATCHER.parent / "truckbox-ingest-v24-v020.patch").read_text()
        for forbidden in ("KNOWN_CORE", "token_hash:", "SUPABASE_SERVICE_ROLE_KEY", "auth_reject"):
            self.assertNotIn(forbidden, delta)
        self.assertIn("isOperationalRow", delta)
        self.assertIn("captureUptimeMs", delta)


if __name__ == "__main__":
    unittest.main()
