#!/usr/bin/env python3
"""Development-only Codex Micro hardware-in-the-loop runner.

The script never builds firmware. It talks to the CH343/UART0 development
console and records machine-readable evidence from the running board.
"""

from __future__ import annotations

import argparse
import json
import queue
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import serial


@dataclass
class Reply:
    received_at: float
    payload: dict
    raw: str


class HilSerial:
    def __init__(self, port: str, baud: int, output: Path) -> None:
        self.serial = serial.Serial(port, baud, timeout=0.1)
        self.replies: queue.Queue[Reply] = queue.Queue()
        self.output = output
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.log = self.output.open("a", encoding="utf-8")
        self.stop = threading.Event()
        self.reader = threading.Thread(target=self._reader, daemon=True)
        self.reader.start()

    def _write_record(self, kind: str, value: object) -> None:
        record = {
            "time": datetime.now(timezone.utc).isoformat(),
            "monotonic": time.monotonic(),
            "kind": kind,
            "value": value,
        }
        self.log.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.log.flush()

    def _reader(self) -> None:
        pending = bytearray()
        while not self.stop.is_set():
            raw = self.serial.read(self.serial.in_waiting or 1)
            if not raw:
                continue
            pending.extend(raw)
            while (newline := pending.find(b"\n")) >= 0:
                raw_line = bytes(pending[:newline])
                del pending[:newline + 1]
                line = raw_line.decode("utf-8", errors="replace").strip()
                self._write_record("uart", line)
                marker = line.find("@hil:")
                if marker < 0:
                    continue
                encoded = line[marker + 5 :]
                try:
                    payload = json.loads(encoded)
                except json.JSONDecodeError:
                    self._write_record("parse_error", encoded)
                    continue
                self.replies.put(Reply(time.monotonic(), payload, line))

    @staticmethod
    def _expected_key(text: str) -> str:
        return {
            "@hil ping": "pong",
            "@hil snapshot": "snapshot",
            "@hil metrics": "metrics",
            "@hil trace": "trace_begin",
            "@hil screen crc": "screen_crc",
            "@hil routing snapshot": "routing",
        }.get(text, "accepted")

    def command(self, text: str, timeout: float = 2.0) -> Reply:
        while not self.replies.empty():
            self.replies.get_nowait()
        self._write_record("command", text)
        self.serial.write((text + "\n").encode("ascii"))
        self.serial.flush()
        expected_key = self._expected_key(text)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                reply = self.replies.get(timeout=min(0.1, deadline - time.monotonic()))
            except queue.Empty:
                continue
            if "trace" in reply.payload or "screen_row" in reply.payload:
                continue
            if reply.payload.get("ok") is False:
                raise RuntimeError(f"{text}: {reply.payload}")
            if expected_key not in reply.payload:
                self._write_record(
                    "stale_reply",
                    {
                        "command": text,
                        "expected": expected_key,
                        "payload": reply.payload,
                    },
                )
                continue
            return reply
        raise TimeoutError(f"no HIL reply for {text!r}")

    def close(self) -> None:
        self.stop.set()
        self.reader.join(timeout=1)
        self.serial.close()
        self.log.close()


def snapshot(link: HilSerial) -> dict:
    return link.command("@hil snapshot").payload["snapshot"]


def metrics(link: HilSerial) -> dict:
    return link.command("@hil metrics").payload["metrics"]


def wait_snapshot(link: HilSerial, predicate, timeout: float = 2.0) -> dict:
    deadline = time.monotonic() + timeout
    latest = {}
    while time.monotonic() < deadline:
        latest = snapshot(link)
        if predicate(latest):
            return latest
        time.sleep(0.04)
    raise AssertionError(f"snapshot condition not met: {latest}")


def wait_display_resumed(link: HilSerial, timeout: float = 12.0) -> dict:
    """Wait for real scanout progress, not only the reducer power enum."""
    deadline = time.monotonic() + timeout
    previous_vsync = None
    latest = {}
    while time.monotonic() < deadline:
        latest = metrics(link)
        display = latest["display"]
        framebuffer_ready = display["fb"] != "0x00000000"
        vsync = display["vsync"]
        if (
            framebuffer_ready
            and previous_vsync is not None
            and vsync > previous_vsync
        ):
            return latest
        previous_vsync = vsync
        time.sleep(0.08)
    raise AssertionError(f"display did not resume scanout: {latest}")


def measure_interaction_latency(link: HilSerial, rounds: int = 6) -> dict:
    host_samples = []
    device_samples = []

    def record(started: float) -> None:
        host_samples.append(round((time.monotonic() - started) * 1000, 2))
        device_samples.append(
            round(metrics(link)["input_latency_us"]["last"] / 1000, 3)
        )

    for index in range(rounds):
        before = snapshot(link)["input_count"]
        started = time.monotonic()
        link.command(f"@hil joystick {(index * 43) % 360} 0.82")
        wait_snapshot(link, lambda state: state["input_count"] >= before + 1)
        record(started)

        before = snapshot(link)["input_count"]
        started = time.monotonic()
        link.command("@hil encoder 1")
        wait_snapshot(link, lambda state: state["input_count"] >= before + 1)
        record(started)

        before = snapshot(link)["input_count"]
        started = time.monotonic()
        link.command("@hil key down command0")
        wait_snapshot(link, lambda state: state["input_count"] >= before + 1)
        link.command("@hil key up command0")
        wait_snapshot(link, lambda state: state["input_count"] >= before + 2)
        record(started)

    ordered = sorted(device_samples)
    host_ordered = sorted(host_samples)
    return {
        "device_samples": device_samples,
        "host_samples": host_samples,
        "p50": ordered[len(ordered) // 2],
        "p95": ordered[
            min(len(ordered) - 1, int(len(ordered) * 0.95))
        ],
        "maximum": ordered[-1],
        "host_p95": host_ordered[
            min(len(host_ordered) - 1, int(len(host_ordered) * 0.95))
        ],
    }


def run(args: argparse.Namespace) -> None:
    link = HilSerial(args.port, args.baud, args.output)
    failures: list[str] = []
    try:
        # Opening CH343 can assert DTR/RTS and reset the ESP32-S3. Commands
        # sent before BOARD_READY are intentionally not assumed to survive.
        boot_deadline = time.monotonic() + 15
        pong = None
        while time.monotonic() < boot_deadline:
            try:
                pong = link.command("@hil ping", timeout=1)
                break
            except TimeoutError:
                time.sleep(0.15)
        if pong is None:
            raise TimeoutError("board did not expose UART HIL within 15 seconds")
        assert pong.payload.get("pong") is True

        initial = snapshot(link)
        initial_metrics = metrics(link)
        if initial["power"] != 0:
            link.command("@hil power")
            initial = wait_snapshot(link, lambda state: state["power"] == 0)
        if initial["layer"] != 1:
            link.command("@hil layer 1")
            initial = wait_snapshot(link, lambda state: state["layer"] == 1)

        # A physical/encrypted BLE link is not a successful Codex connection.
        # The App must have delivered a control-plane report and the firmware
        # must have reached Ready. This catches stale Windows GATT handles,
        # pre-auth loss and HID-output routing failures.
        if initial["transport"] == 2:
            try:
                initial = wait_snapshot(
                    link,
                    lambda state: not state["connected"]
                    or state["codex_ready"] is True,
                    timeout=args.ready_timeout,
                )
            except AssertionError:
                failures.append(
                    "P0: BLE link connected but Codex control plane never "
                    "reached Ready"
                )
            link.command("@hil trace")

        # Exercise the real hit-test/input-normalizer path, not a UI-only flag.
        link.command("@hil touch down 0 237 384")
        wait_snapshot(link, lambda state: state["mic_pressed"] is True)
        time.sleep(1.2)
        held = snapshot(link)
        if held["mic_pressed"] is not True:
            failures.append("P0: MIC released while touch remained stationary")
        link.command("@hil touch up 0")
        wait_snapshot(link, lambda state: state["mic_pressed"] is False)

        # Activate the private App lease, configure three-layer local routing,
        # and prove that Layer 2 AG00 is held as flat App id 6. No layer field
        # crosses the private control transport.
        link.command("@hil app connect")
        wait_snapshot(link, lambda state: state["app"]["active"] is True)
        link.command("@hil routing layers 3")
        link.command("@hil routing layer1 4 2")
        link.command("@hil routing passthrough 2 agent 0x02")
        routed = wait_snapshot(
            link,
            lambda state: state["routing"]["enabled"] is True
            and state["routing"]["layers"] == 3,
        )
        link.command("@hil layer 2")
        wait_snapshot(link, lambda state: state["layer"] == 2)
        link.command("@hil key down ag00")
        held_route = wait_snapshot(
            link, lambda state: int(state["app"]["held"][0], 16) & (1 << 6)
        )
        if int(held_route["app"]["held"][0], 16) & (1 << 6) == 0:
            failures.append("Layer 2 AG00 did not route to flat App id 6")
        link.command("@hil key up ag00")
        wait_snapshot(
            link, lambda state: int(state["app"]["held"][0], 16) == 0
        )
        link.command("@hil lighting app agent 6 4 1 0.5 0 0x304ffe")
        link.command("@hil app heartbeat")
        link.command("@hil app disconnect")
        fallback = wait_snapshot(
            link,
            lambda state: state["app"]["active"] is False,
        )
        if fallback["routing"]["enabled"] is not True:
            failures.append("routing preferences were lost after App disconnect")

        # Verify the latency-critical queue does not lose a burst of joystick
        # and release events.
        input_before = snapshot(link)["input_count"]
        started = time.monotonic()
        for index in range(20):
            link.command(f"@hil joystick {(index * 17) % 360} 0.80")
        link.command("@hil joystick 0 0")
        burst_ms = (time.monotonic() - started) * 1000
        input_after = wait_snapshot(
            link, lambda state: state["input_count"] >= input_before + 21
        )
        if burst_ms > 1500:
            failures.append(f"input burst took {burst_ms:.0f} ms")
        if input_after["hid"]["failed"] > initial["hid"]["failed"]:
            failures.append("HID failure count increased during input burst")

        # Maximum visual load: six breathing agents plus ambient snake. The
        # monitor reads the active RGB565 scanout buffer continuously.
        link.command("@hil lighting stress off")
        time.sleep(1.0)
        idle_visual_start = metrics(link)
        time.sleep(2.0)
        idle_visual_end = metrics(link)
        idle_input_latency = measure_interaction_latency(link)
        link.command("@hil lighting stress on")
        time.sleep(0.5)
        full_visual_start = metrics(link)
        time.sleep(2.0)
        full_visual_end = metrics(link)
        stress_start = metrics(link)
        black_start = stress_start["display"]["black_faults"]
        timing_start = stress_start["display"]["timing_faults"]
        slow_start = stress_start["slow_frames"]
        full_light_input_latency = measure_interaction_latency(link)
        latency_p95 = full_light_input_latency["p95"]
        if latency_p95 > 100.0:
            failures.append(
                f"full-light input latency P95 exceeded 100 ms "
                f"({latency_p95:.2f} ms)"
            )
        deadline = time.monotonic() + args.stress_seconds
        samples = []
        while time.monotonic() < deadline:
            sample = metrics(link)
            samples.append(sample)
            time.sleep(0.8)
        link.command("@hil lighting stress off")
        stress_end = metrics(link)

        if stress_end["display"]["black_faults"] != black_start:
            failures.append(
                "display monitor detected black-block corruption "
                f"({black_start}->{stress_end['display']['black_faults']})"
            )
        if stress_end["display"]["timing_faults"] != timing_start:
            failures.append(
                "VSYNC timing fault count increased "
                f"({timing_start}->{stress_end['display']['timing_faults']})"
            )
        if stress_end["slow_frames"] - slow_start > args.max_slow_frames:
            failures.append(
                "slow-frame budget exceeded "
                f"({slow_start}->{stress_end['slow_frames']})"
            )
        if (
            stress_end["ble"]["started"]
            and stress_end["ble"]["rx_stack_low_water"] < 2048
        ):
            failures.append(
                "P0: BLE RX stack reserve fell below 2048 bytes "
                f"({stress_end['ble']['rx_stack_low_water']} bytes)"
            )

        crc = link.command("@hil screen crc").payload["screen_crc"]["tiles"]
        if len(crc) != 36 or any(value == "00000000" for value in crc):
            failures.append("not all 36 framebuffer tiles received a CRC")

        power_cycles = []
        link.command("@hil power")
        wait_snapshot(link, lambda state: state["power"] == 1, timeout=5)
        link.command("@hil power")
        wait_snapshot(link, lambda state: state["power"] == 0, timeout=5)
        time.sleep(0.4)
        power_cycles.append({"mode": "connected", "metrics": metrics(link)})

        for _ in range(2):
            link.command("@hil ultra")
            wait_snapshot(link, lambda state: state["power"] == 2, timeout=5)
            link.command("@hil wake")
            wait_snapshot(link, lambda state: state["power"] == 0, timeout=8)
            resumed_metrics = wait_display_resumed(link, timeout=12)
            power_cycles.append({"mode": "ultra", "metrics": resumed_metrics})
            if resumed_metrics["touch_gap_us"]["last"] > 30000:
                failures.append(
                    "touch sampler did not resume within 30 ms after ultra"
                )

        summary = {
            "initial": initial,
            "initial_metrics": initial_metrics,
            "mic_held": held,
            "routed": routed,
            "routed_press": held_route,
            "route_fallback": fallback,
            "idle_visual_metrics": {
                "start": idle_visual_start,
                "end": idle_visual_end,
            },
            "full_light_visual_metrics": {
                "start": full_visual_start,
                "end": full_visual_end,
            },
            "idle_input_latency_ms": idle_input_latency,
            "full_light_input_latency_ms": full_light_input_latency,
            "stress_samples": samples,
            "power_cycles": power_cycles,
            "final_metrics": stress_end,
            "failures": failures,
        }
        link._write_record("summary", summary)
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(
            json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
        )
    finally:
        try:
            link.command("@hil lighting stress off", timeout=0.5)
            link.command("@hil touch cancel 0", timeout=0.5)
        except Exception:
            pass
        link.close()

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS HIL evidence={args.output} summary={args.summary}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--stress-seconds", type=float, default=30)
    parser.add_argument("--max-slow-frames", type=int, default=4)
    parser.add_argument("--ready-timeout", type=float, default=12)
    parser.add_argument(
        "--output", type=Path, default=Path("diagnostics/hil/latest/uart.jsonl")
    )
    parser.add_argument(
        "--summary", type=Path, default=Path("diagnostics/hil/latest/summary.json")
    )
    return parser.parse_args()


if __name__ == "__main__":
    run(parse_args())
