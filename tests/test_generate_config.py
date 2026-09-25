from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("generate_config", ROOT / "scripts" / "generate_config.py")
assert SPEC and SPEC.loader
generate_config = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generate_config)


class GeneratorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.data = json.loads((ROOT / "config" / "lab.json").read_text())

    def generate(self, data: dict) -> tuple[Path, tempfile.TemporaryDirectory[str]]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        source = root / "lab.json"
        source.write_text(json.dumps(data))
        output = root / "generated"
        generate_config.produce(source, output, False)
        return output, temporary

    def test_defaults_generate_all_endpoints(self) -> None:
        output, temporary = self.generate(self.data)
        self.addCleanup(temporary.cleanup)
        topology = (output / "four-radio.clab.yml").read_text()
        processor = json.loads((output / "processor.json").read_text())
        detector = json.loads((output / "detector.json").read_text())
        self.assertEqual(7, topology.count("- endpoints:"))
        self.assertTrue(all(radio["iq"]["bind"] == "0.0.0.0" for radio in processor["radios"]))
        self.assertEqual("0.0.0.0", detector["spectrum"]["bind"])
        self.assertEqual([18401, 18402, 18403, 18404], [radio["control"]["port"] for radio in processor["radios"]])
        self.assertEqual([18701, 18702, 18703, 18704], [radio["status"]["port"] for radio in processor["radios"]])
        self.assertTrue(all(radio["settings"] == {
            "center_hz": 100000000,
            "sample_rate_hz": 1000000,
            "bandwidth_hz": 800000,
            "gain_db_q7": 0,
        } for radio in processor["radios"]))
        self.assertIn("sha256:bc8112667b5a87bee5039ade65b504ac2ef35511210d0675db6c7b0754e8cc4c", topology)
        switch = (output / "srlinux.cli").read_text()
        self.assertIn("subinterface 0 type local-mirror-dest", switch)
        self.assertIn("local-mirror-destination admin-state enable", switch)
        self.assertEqual(topology.count("memory: 67108864b"), 7)
        self.assertEqual(topology.count("privileged: false"), 7)
        self.assertEqual(topology.count("restart-policy: no"), 7)
        self.assertEqual(6, switch.count("subinterface 0 l2-mtu 9000"))
        self.assertIn("/ system mirroring mirroring-instance capture", switch)
        self.assertNotIn("traffic-mirroring", switch)

    def test_override_updates_radio_and_processor(self) -> None:
        changed = copy.deepcopy(self.data)
        changed["radios"][2]["address"] = "10.79.0.42/24"
        changed["radios"][2]["control_port"] = 19403
        output, temporary = self.generate(changed)
        self.addCleanup(temporary.cleanup)
        radio = json.loads((output / "radio3.json").read_text())
        processor = json.loads((output / "processor.json").read_text())
        self.assertEqual("10.79.0.42/24", radio["radio"]["address"])
        self.assertEqual("10.79.0.42", processor["radios"][2]["control"]["host"])
        self.assertEqual(19403, processor["radios"][2]["control"]["port"])
        self.assertIn("10.79.0.42/24", (output / "four-radio.clab.yml").read_text())

    def test_conflicting_assignment_is_rejected(self) -> None:
        changed = copy.deepcopy(self.data)
        changed["radios"][1]["iq_port"] = changed["radios"][0]["control_port"]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "lab.json"
            source.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, "ports must be unique"):
                generate_config.load_and_validate(source)

    def test_check_rejects_stale_output(self) -> None:
        output, temporary = self.generate(self.data)
        self.addCleanup(temporary.cleanup)
        source = Path(temporary.name) / "lab.json"
        generate_config.produce(source, output, True)
        (output / "radio1.json").write_text("{}\n")
        with self.assertRaises(SystemExit):
            generate_config.produce(source, output, True)


if __name__ == "__main__":
    unittest.main()
