#!/usr/bin/env python3
"""Build and publish Cinder KISS firmware artifacts to cinder-gateway."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIO = Path.home() / ".platformio" / "penv" / "bin" / "pio"

ENV_TARGETS = {
    "Heltec_v3_kiss_modem": {
        "target": "heltec-v3",
        "artifact": "firmware.bin",
    },
    "heltec_v4_kiss_modem": {
        "target": "heltec-v4",
        "artifact": "firmware.bin",
    },
    "LilyGo_T-Echo_kiss_modem": {
        "target": "t-echo",
        "artifact": "firmware.zip",
    },
}


def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, cwd=ROOT, text=True).strip()


def firmware_version() -> int:
    header = ROOT / "examples" / "kiss_modem" / "KissModem.h"
    match = re.search(r"#define\s+KISS_FIRMWARE_VERSION\s+(\d+)", header.read_text())
    if not match:
        raise SystemExit("KISS_FIRMWARE_VERSION not found")
    return int(match.group(1))


def git_commit() -> str:
    try:
        return run(["git", "rev-parse", "HEAD"])
    except Exception:
        return ""


def build_env(env: str) -> None:
    pio = str(PIO if PIO.exists() else shutil.which("pio") or "pio")
    subprocess.check_call([pio, "run", "-e", env], cwd=ROOT)


def multipart(fields: dict[str, str], file_path: Path) -> tuple[bytes, str]:
    boundary = f"----cinder-{uuid.uuid4().hex}"
    chunks: list[bytes] = []
    for key, value in fields.items():
        chunks.append(f"--{boundary}\r\n".encode())
        chunks.append(f'Content-Disposition: form-data; name="{key}"\r\n\r\n'.encode())
        chunks.append(str(value).encode())
        chunks.append(b"\r\n")
    chunks.append(f"--{boundary}\r\n".encode())
    chunks.append(
        (
            f'Content-Disposition: form-data; name="file"; filename="{file_path.name}"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n"
        ).encode()
    )
    chunks.append(file_path.read_bytes())
    chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode())
    return b"".join(chunks), f"multipart/form-data; boundary={boundary}"


def upload(gateway: str, env: str, version: str, fw_number: int) -> None:
    target_info = ENV_TARGETS[env]
    artifact_path = ROOT / ".pio" / "build" / env / target_info["artifact"]
    if not artifact_path.exists():
        raise SystemExit(f"missing {artifact_path}; run with --build or build this env first")

    fields = {
        "target": target_info["target"],
        "env": env,
        "version": version,
        "firmware_version": str(fw_number),
        "git_commit": git_commit(),
        "notes": f"published from MeshCore at {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}",
    }
    body, content_type = multipart(fields, artifact_path)
    url = gateway.rstrip("/") + "/api/firmware/artifacts"
    request = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": content_type, "Content-Length": str(len(body))},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        payload = json.loads(response.read().decode())
    artifact = payload["artifact"]
    print(
        f"uploaded {env} -> {artifact['artifact_id']} "
        f"target={target_info['target']} version={version} sha256={artifact['sha256'][:16]}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway", default=os.environ.get("CINDER_GATEWAY", "http://10.0.0.68:9080"))
    parser.add_argument("--env", action="append", choices=sorted(ENV_TARGETS), required=True)
    parser.add_argument("--build", action="store_true", help="run PlatformIO build before upload")
    parser.add_argument("--version", help="gateway display version, defaults to firmware hex like 0A00")
    parser.add_argument("--firmware-version", type=int, help="numeric KISS firmware version")
    args = parser.parse_args()

    fw_number = args.firmware_version or firmware_version()
    version = args.version or f"{fw_number:02X}00"
    for env in args.env:
        if args.build:
            build_env(env)
        upload(args.gateway, env, version, fw_number)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
