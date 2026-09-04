#!/usr/bin/env python3
"""Build a fail-closed evidence manifest for the first production device.

This tool does not flash firmware, provision eFuses, or manufacture evidence.
It verifies already-produced case records and their raw artifacts, then emits
the manifest consumed by ``tools/release/audit_completion.py``.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from typing import Any, Sequence

from tools.release.audit_completion import _requirements


COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
DEVICE_ID_RE = re.compile(r"^[0-9a-f]{32}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
NAME_RE = re.compile(r"^[^\x00-\x1f]{1,96}$")


def _timestamp(value: object) -> datetime | None:
    if not isinstance(value, str) or not value.endswith("Z"):
        return None
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError:
        return None
    return parsed if parsed.utcoffset() is not None else None


def _inside(root: Path, path: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
    except (OSError, ValueError):
        return False
    return True


def _artifact_findings(
    requirement_id: str,
    artifacts: object,
    evidence_root: Path,
) -> list[str]:
    if not isinstance(artifacts, list) or not 1 <= len(artifacts) <= 64:
        return [f"{requirement_id} artifacts must contain 1 through 64 items"]
    findings: list[str] = []
    seen: set[str] = set()
    for artifact in artifacts:
        if not isinstance(artifact, dict):
            findings.append(f"{requirement_id} artifact must be an object")
            continue
        raw_path = artifact.get("path")
        if (
            not isinstance(raw_path, str)
            or not raw_path
            or Path(raw_path).is_absolute()
            or ".." in Path(raw_path).parts
            or raw_path in seen
        ):
            findings.append(
                f"{requirement_id} artifact path is invalid: {raw_path}"
            )
            continue
        seen.add(raw_path)
        target = evidence_root / raw_path
        if not _inside(evidence_root, target) or target.is_symlink():
            findings.append(
                f"{requirement_id} artifact path is invalid: {raw_path}"
            )
            continue
        if not target.is_file():
            findings.append(
                f"{requirement_id} artifact is missing: {raw_path}"
            )
            continue
        expected = artifact.get("sha256")
        if not isinstance(expected, str) or SHA256_RE.fullmatch(expected) is None:
            findings.append(
                f"{requirement_id} artifact sha256 is invalid: {raw_path}"
            )
            continue
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if actual != expected:
            findings.append(
                f"{requirement_id} artifact hash mismatch: {raw_path}"
            )
    return findings


def _case_findings(
    requirement_id: str,
    expected_kind: str,
    record: object,
    evidence_root: Path,
    source_commit: str,
    device_id: str,
) -> list[str]:
    if not isinstance(record, dict) or record.get("schema") != 1:
        return [f"{requirement_id} has an unsupported case schema"]
    findings: list[str] = []
    if record.get("requirement_id") != requirement_id:
        findings.append(f"{requirement_id} case ID mismatch")
    if record.get("kind") != expected_kind:
        findings.append(f"{requirement_id} case kind mismatch")
    if record.get("status") != "passed":
        findings.append(f"{requirement_id} case did not pass")
    if record.get("source_commit") != source_commit:
        findings.append(f"{requirement_id} belongs to a different source commit")
    if expected_kind == "device" and record.get("device_id") != device_id:
        findings.append(f"{requirement_id} belongs to a different device")

    producer = record.get("producer")
    operator = record.get("operator")
    if producer not in {"automated", "manual"}:
        findings.append(f"{requirement_id} producer is invalid")
    if not isinstance(operator, str) or NAME_RE.fullmatch(operator) is None:
        findings.append(f"{requirement_id} operator is invalid")
    if producer == "manual":
        reviewer = record.get("reviewer")
        if (
            not isinstance(reviewer, str)
            or NAME_RE.fullmatch(reviewer) is None
            or reviewer == operator
        ):
            findings.append(
                f"{requirement_id} manual evidence requires a distinct reviewer"
            )

    started = _timestamp(record.get("started_at"))
    finished = _timestamp(record.get("finished_at"))
    if started is None or finished is None or finished < started:
        findings.append(f"{requirement_id} timestamps are invalid")
    findings.extend(
        _artifact_findings(requirement_id, record.get("artifacts"), evidence_root)
    )
    return findings


def _read_json(path: Path) -> tuple[object, str | None]:
    try:
        return json.loads(path.read_text(encoding="utf-8")), None
    except (OSError, json.JSONDecodeError) as error:
        return {}, str(error)


def _write_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(encoded)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def build_release_evidence(
    *,
    ledger_path: Path,
    cases_directory: Path,
    evidence_root: Path,
    source_commit: str,
    device_id: str,
    output_path: Path,
    created_at: str,
) -> list[str]:
    """Verify all release cases and atomically write audit evidence.

    The returned list is empty on success. Any finding prevents output.
    """

    evidence_root = evidence_root.resolve()
    cases_directory = cases_directory.resolve()
    output_path = output_path.absolute()
    findings: list[str] = []
    if COMMIT_RE.fullmatch(source_commit) is None:
        findings.append("source commit must be 40 lowercase hexadecimal characters")
    if DEVICE_ID_RE.fullmatch(device_id) is None:
        findings.append("device ID must be 32 lowercase hexadecimal characters")
    if _timestamp(created_at) is None:
        findings.append("created_at must be an RFC3339 UTC timestamp")
    if not _inside(evidence_root, cases_directory):
        findings.append("cases directory must be inside the evidence root")
    if output_path.parent.resolve() != evidence_root:
        findings.append("output manifest must be directly inside the evidence root")
    elif output_path.is_file() or output_path.is_symlink():
        output_path.unlink()

    requirements, ledger_findings = _requirements(ledger_path)
    findings.extend(ledger_findings)
    requirement_by_id = {
        requirement.requirement_id: requirement for requirement in requirements
    }
    case_paths = {
        path.stem: path
        for path in sorted(cases_directory.glob("*.json"))
        if path.is_file() and not path.is_symlink()
    }
    for unknown in sorted(set(case_paths) - set(requirement_by_id)):
        findings.append(f"unknown case record {unknown}")

    records: dict[str, dict[str, str]] = {}
    for requirement in requirements:
        case_path = case_paths.get(requirement.requirement_id)
        if case_path is None:
            findings.append(f"{requirement.requirement_id} has no case record")
            continue
        record, read_error = _read_json(case_path)
        if read_error is not None:
            findings.append(
                f"{requirement.requirement_id} case cannot be read: {read_error}"
            )
            continue
        findings.extend(
            _case_findings(
                requirement.requirement_id,
                requirement.kind,
                record,
                evidence_root,
                source_commit,
                device_id,
            )
        )
        try:
            relative_case = case_path.relative_to(evidence_root).as_posix()
        except ValueError:
            findings.append(
                f"{requirement.requirement_id} case is outside the evidence root"
            )
            continue
        records[requirement.requirement_id] = {
            "kind": requirement.kind,
            "status": "passed",
            "source_commit": source_commit,
            "artifact": relative_case,
            "sha256": hashlib.sha256(case_path.read_bytes()).hexdigest(),
        }

    if findings:
        return findings
    manifest: dict[str, Any] = {
        "schema": 1,
        "source_commit": source_commit,
        "device_id": device_id,
        "created_at": created_at,
        "requirements": records,
    }
    _write_atomic(output_path, manifest)
    return []


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify release case records and build completion evidence."
    )
    parser.add_argument("--ledger", required=True, type=Path)
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--evidence-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--created-at", required=True)
    arguments = parser.parse_args(argv)
    findings = build_release_evidence(
        ledger_path=arguments.ledger,
        cases_directory=arguments.cases,
        evidence_root=arguments.evidence_root,
        source_commit=arguments.source_commit,
        device_id=arguments.device_id,
        output_path=arguments.output,
        created_at=arguments.created_at,
    )
    print(
        json.dumps(
            {"ok": not findings, "findings": findings},
            ensure_ascii=False,
            sort_keys=True,
        )
    )
    return 0 if not findings else 1


if __name__ == "__main__":
    raise SystemExit(main())
