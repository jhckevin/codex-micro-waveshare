from pathlib import Path


root = Path(__file__).resolve().parents[2]
runner = (root / "tools/hil/hil_runner.py").read_text(encoding="utf-8")

assert '"codex_ready"' in runner
assert "P0: BLE link connected but Codex control plane never " in runner
assert "reached Ready" in runner
assert "--ready-timeout" in runner
assert "def wait_display_resumed" in runner
assert 'display["fb"] != "0x00000000"' in runner
assert "vsync > previous_vsync" in runner
assert "resumed_metrics = wait_display_resumed" in runner
