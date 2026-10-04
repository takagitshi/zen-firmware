#!/usr/bin/env python3
"""Exercise automatic AML generation with editable Mouse bindings."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from importlib.util import module_from_spec, spec_from_file_location

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from aml_keymap import aml_header, bindings, keymap_source, layer_body, mouse_positions
spec = spec_from_file_location("verify_zen", ROOT / "scripts/verify-zen-build.py")
verifier = module_from_spec(spec)
spec.loader.exec_module(verifier)


def replace_mouse(text, items):
    body = layer_body(text, "layer_1")
    changed = re.sub(r"(?<!sensor-)\bbindings\s*=\s*<.*?>;",
                     "bindings = <" + " ".join(items) + ">;", body, count=1, flags=re.DOTALL)
    return text.replace(body, changed, 1)


class AutomaticAML(unittest.TestCase):
    def setUp(self):
        self.keymap = (ROOT / "config/keymap.keymap").read_text()

    def verify(self, text):
        original = verifier.read_text
        verifier.read_text = lambda path: text if path == ROOT / "config/keymap.keymap" else original(path)
        try:
            verifier.verify_sources(ROOT)
        finally:
            verifier.read_text = original

    def test_current_and_wrapper(self):
        source, inputs = keymap_source(ROOT / "boards/shields/zen/zen.keymap")
        self.assertEqual(source, self.keymap)
        self.assertEqual(len(inputs), 2)
        self.assertEqual(mouse_positions(source, key_count=50), mouse_positions(self.keymap, key_count=50))
        self.verify(source)

    def test_add_remove_move(self):
        items = ["&trans"] * 50
        items[19], items[22] = "&kp F13", "&kp F14"
        original = replace_mouse(self.keymap, items)
        self.assertEqual(mouse_positions(original, key_count=50), [19, 22])
        items[0], items[22] = items[22], "&trans"
        changed = replace_mouse(self.keymap, items)
        self.assertEqual(mouse_positions(changed, key_count=50), [0, 19])
        self.verify(changed)

    def test_empty_mouse(self):
        changed = replace_mouse(self.keymap, ["&trans", "&none"] * 25)
        self.assertEqual(mouse_positions(changed, key_count=50), [])
        self.assertIn("AML_EXCLUDED_POSITIONS 65535", aml_header(changed, key_count=50))
        self.verify(changed)

    def test_comments_multiline_and_sensor_binding(self):
        changed = self.keymap.replace("&mkp MB1", "&mkp\n MB4 /* &kp F1 */", 1)
        changed = changed.replace('display-name = "Mouse";', 'display-name = "Mouse"; sensor-bindings = <&none>;')
        self.assertEqual(mouse_positions(changed, key_count=50), mouse_positions(self.keymap, key_count=50))
        self.verify(changed)

    def test_invalid_slot_count(self):
        changed = replace_mouse(self.keymap, ["&trans"] * 49)
        with self.assertRaisesRegex(AssertionError, "50 editable slots"):
            mouse_positions(changed, key_count=50)

    def test_wrapper_cycle(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrapper.keymap"
            path.write_text('#include "wrapper.keymap"')
            with self.assertRaisesRegex(AssertionError, "cyclic"):
                keymap_source(path)

    def test_generator_idempotence_inputs_and_no_source_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            keymap, wrapper = path / "edited.keymap", path / "wrapper.keymap"
            output, deps = path / "aml.h", path / "inputs.txt"
            wrapper.write_text('#include "edited.keymap"')
            keymap.write_text(self.keymap)
            command = [sys.executable, str(ROOT / "scripts/generate-aml-exclusions.py"),
                       str(wrapper), str(output), "--key-count", "50", "--depfile", str(deps)]
            subprocess.run(command, check=True)
            timestamp = output.stat().st_mtime_ns
            subprocess.run(command, check=True)
            self.assertEqual(output.stat().st_mtime_ns, timestamp)
            self.assertEqual(keymap.read_text(), self.keymap)
            self.assertEqual(deps.read_text().splitlines(), [str(wrapper.resolve()), str(keymap.resolve())])
            changed = replace_mouse(self.keymap, ["&trans"] * 50)
            keymap.write_text(changed)
            subprocess.run(command, check=True)
            self.assertEqual(output.read_text(), aml_header(changed, key_count=50))
            self.assertEqual(keymap.read_text(), changed)


if __name__ == "__main__":
    unittest.main()
