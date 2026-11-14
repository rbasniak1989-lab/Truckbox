"""Guards for the review-only Cloud patch: stale source must never be replaced."""
from pathlib import Path
import importlib.util
import tempfile
import unittest
import hashlib

path=Path(__file__).resolve().parents[1]/"cloud/prepare_fuel_v2.py"
spec=importlib.util.spec_from_file_location("fuel_v2_patch",path)
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)

class FuelPatchTests(unittest.TestCase):
    def test_unreviewed_source_is_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            source,out,diff=(Path(d)/n for n in ('source.ts','review.ts','review.patch'))
            source.write_text('unreviewed')
            with self.assertRaisesRegex(ValueError,'SHA256 differs'):patch.prepare(source,out,diff)
            self.assertEqual(source.read_text(),'unreviewed');self.assertFalse(out.exists());self.assertFalse(diff.exists())

    def test_output_aliases_cannot_destroy_input(self):
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'source.ts';source.write_text('keep')
            for out,diff in ((source,Path(d)/'diff'),(Path(d)/'out',source)):
                with self.assertRaisesRegex(ValueError,'must differ'):patch.prepare(source,out,diff)
            self.assertEqual(source.read_text(),'keep')

    def test_data_policy_only_no_auth_or_transport_change(self):
        fixture='// authentication and ACK sentinel\n'+'\n'.join(x for x,_ in patch.REPLACEMENTS)
        oldhash=patch.BASE_SHA256
        try:
            patch.BASE_SHA256=hashlib.sha256((fixture.rstrip()+'\n').encode()).hexdigest()
            with tempfile.TemporaryDirectory() as d:
                source,out,diff=(Path(d)/n for n in ('source.ts','review.ts','review.patch'))
                source.write_text(fixture);patch.prepare(source,out,diff)
                self.assertEqual(source.read_text(),fixture)
                self.assertIn('// authentication and ACK sentinel',out.read_text())
                self.assertIn('normalizeCoreFuelV2',out.read_text())
                self.assertIn('cloud_rate_applied: true',out.read_text())
        finally:patch.BASE_SHA256=oldhash

    def test_public_diff_does_not_contain_authentication_block(self):
        diff=path.with_name('truckbox-ingest-fuel-v2.patch').read_text()
        for s in ('SUPABASE_SERVICE_ROLE_KEY','token_hash:','auth_reject','KNOWN_CORE'):
            self.assertNotIn(s,diff)

if __name__=='__main__':unittest.main()
