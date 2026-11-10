"""Execute the embedded production panel with a DOM boundary and no network."""
import re
import shutil
import subprocess
import unittest
from pathlib import Path

PAGE = Path(__file__).resolve().parents[1] / "components/tb_web/index.html"


@unittest.skipUnless(shutil.which("node"), "Node required for embedded panel test")
class J1708PanelTests(unittest.TestCase):
    def test_embedded_script_syntax(self):
        script = re.search(r"<script>(.*?)</script>", PAGE.read_text(), re.S).group(1)
        subprocess.run(["node", "--check"], input=script, text=True, check=True, capture_output=True)

    def test_work_mode_gate_and_emergency_stop(self):
        script = re.search(r"  function updateJ1708Panel\(\) \{.*?\n  \}", PAGE.read_text(), re.S).group(0)
        boundary = """
const assert=require('node:assert/strict');
const elements=new Map();
const $=id=>{if(!elements.has(id))elements.set(id,{value:'',checked:false,disabled:false});return elements.get(id);};
const text=()=>{};
let role='core',token='SYNTHETIC_TEST_TOKEN',uploadBusy=false,j1708Busy=false,stateBusy=false;
let lastState={speedKph:0,ota_busy:false,j1708:{ready:true,observed_mids:[128,130,140,144],
    requests:{armed:true,work_mode:false,work_evidence_ready:false,safety_stopped:false}}};
$('j1708-local-mid').value='172';$('j1708-topology').checked=true;
"""
        cases = """
updateJ1708Panel();assert.equal($('j1708-work').disabled,true);
lastState.j1708.requests.work_evidence_ready=true;
updateJ1708Panel();assert.equal($('j1708-work').disabled,true); // Echo alone never validates hardware.
lastState.j1708.requests.physical_validated=true;
updateJ1708Panel();assert.equal($('j1708-work').disabled,false);
lastState.j1708.requests.armed=false; // Finished parked scan can be promoted.
updateJ1708Panel();assert.equal($('j1708-work').disabled,false);
lastState.speedKph=20;updateJ1708Panel();assert.equal($('j1708-work').disabled,true);
assert.equal($('j1708-stop').disabled,false);
lastState.speedKph=0;lastState.j1708.requests.safety_stopped=true;
updateJ1708Panel();assert.equal($('j1708-work').disabled,true);
lastState.j1708.requests.safety_stopped=false;$('j1708-topology').checked=false;
updateJ1708Panel();assert.equal($('j1708-work').disabled,true);
$('j1708-topology').checked=true;j1708Busy=true;
updateJ1708Panel();assert.equal($('j1708-work').disabled,true);
assert.equal($('j1708-stop').disabled,false);
"""
        subprocess.run(["node"], input=boundary + script + cases, text=True, check=True, capture_output=True)
