"""Reference-model equivalence; generated vectors, not full Mother Logs replay."""
import importlib.util
import pathlib
import random
import subprocess
import sys
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
REF = ROOT.parent / 'j1939/references/truckbox_j1939_parser_v1.py'
spec = importlib.util.spec_from_file_location('truckbox_fuel_reference', REF)
reference = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = reference
spec.loader.exec_module(reference)
class FuelEquivalence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='truckbox-fuel-')
        cls.exe = pathlib.Path(cls.tmp.name) / 'probe'
        core = ROOT / 'components/tb_core'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic',
            '-I'+str(core),str(core/'tbx_fuel.c'),str(core/'tests/fuel_probe.c'),'-lm','-o',str(cls.exe)], check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def run_probe(self, vectors):
        data = ''.join(f'{sa} {rpm} {torque} {at}\n' for sa,rpm,torque,at in vectors)
        return [[float(x) for x in row.split()] for row in subprocess.check_output([str(self.exe)],input=data,text=True).splitlines()]
    def test_python_c_1000_generated_valid_samples(self):
        rng = random.Random(1939)
        vectors=[]; at=0
        for _ in range(1000):
            at += rng.choice([10,20,100,1000,1001])
            vectors.append((0,rng.randint(4000,18000),rng.randint(125,225),at))
        actual=self.run_probe(vectors); model=reference.FuelPowerAccumulator()
        for (sa,rpm,tq,ts),(valid,rate,delta,total) in zip(vectors,actual):
            payload=bytes([0,0,tq,rpm&255,rpm>>8,0,0,0])
            expected=model.ingest(reference.J1939Frame(ts,61444,sa,8,payload))
            self.assertEqual(valid,1)
            self.assertAlmostEqual(rate,expected,places=11)
            self.assertAlmostEqual(total,model.liters,places=11)
    def test_missing_invalid_reset_and_source_change_skip_intervals(self):
        rows=self.run_probe([(0,8000,200,100),(0,8000,200,200),(0,65535,255,300),
            (0,8000,200,400),(3,8000,200,500),(3,8000,200,100),(3,8000,200,150)])
        self.assertGreater(rows[1][2],0)
        self.assertEqual(rows[2][0],0)
        for i in (2,3,4,5): self.assertEqual(rows[i][2],0)
        self.assertGreater(rows[6][2],0)
if __name__=='__main__': unittest.main()
