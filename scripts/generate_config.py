#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
import re
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "config" / "lab.json"
DEFAULT_OUTPUT = ROOT / "generated"
SRL_DIGEST = re.compile(r"^ghcr\.io/nokia/srlinux:[^@]+@sha256:[0-9a-f]{64}$")


def canonical_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True, separators=(",", ": ")) + "\n"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_and_validate(source: Path) -> dict[str, Any]:
    raw = source.read_bytes()
    data = json.loads(raw)
    if data.get("schema") != 1:
        raise ValueError("unsupported lab configuration schema")
    if data.get("application_mtu") != 9000:
        raise ValueError("application_mtu must remain 9000 for acceptance")
    if not SRL_DIGEST.fullmatch(data["images"]["srlinux"]):
        raise ValueError("SR Linux image must include a tag and immutable sha256 digest")

    interval = data.get("telemetry", {}).get("interval_ms", 5000)
    if type(interval) is not int or (interval != 0 and not 100 <= interval <= 60000):
        raise ValueError("telemetry interval_ms must be 0 or100..60000")
    radios = data.get("radios", [])
    if len(radios) != 4 or [radio.get("id") for radio in radios] != [f"radio{i}" for i in range(1, 5)]:
        raise ValueError("exactly radio1 through radio4 are required")

    addressed = radios + [data["processor"], data["detector"]]
    interfaces = [ipaddress.ip_interface(node["address"]) for node in addressed]
    if len({interface.ip for interface in interfaces}) != len(interfaces):
        raise ValueError("application addresses must be unique")
    if len({interface.network for interface in interfaces}) != 1:
        raise ValueError("application addresses must share one subnet")

    ports: list[tuple[str, int]] = []
    for radio in radios:
        ports.extend((("control", radio["control_port"]), ("iq", radio["iq_port"]), ("status", radio["status_port"])))
    ports.append(("spectrum", data["detector"]["spectrum_port"]))
    numeric_ports = [port for _, port in ports]
    if any(not isinstance(port, int) or port < 1024 or port > 65535 for port in numeric_ports):
        raise ValueError("ports must be integers in 1024..65535")
    if len(set(numeric_ports)) != len(numeric_ports):
        raise ValueError("listener ports must be unique")

    defaults = data["radio_defaults"]
    if defaults["samples_per_packet"] != 1024 or defaults["burst_samples"] != 262144:
        raise ValueError("SDR packet and burst sizes must retain the pinned profile defaults")
    if [radio["signal_hz"] for radio in radios] != [100050000, 100100000, 100150000, 100200000]:
        raise ValueError("radio frequencies differ from the pinned reference")
    if [radio["phase_millidegrees"] for radio in radios] != [0, 45000, 90000, 135000]:
        raise ValueError("radio phases differ from the pinned reference")
    return data


def app_config(data: dict[str, Any], radio: dict[str, Any]) -> dict[str, Any]:
    processor_host = str(ipaddress.ip_interface(data["processor"]["address"]).ip)
    return {
        "schema": 1,
        "role": "radio",
        "radio": radio,
        "defaults": data["radio_defaults"],
        "application": {"interface": "eth1", "mtu": data["application_mtu"]},
        "control": {"bind": "0.0.0.0"},
        "iq_destination": {"host": processor_host, "port": radio["iq_port"]},
        "status": {"bind": "0.0.0.0", "port": radio["status_port"]},
        "limits": data["limits"],
    }


def render_topology(data: dict[str, Any]) -> str:
    lines = [
        f"name: {data['lab_name']}",
        "prefix: clab",
        "mgmt:",
        f"  network: {data['lab_name']}-mgmt",
        "  ipv4-subnet: 172.31.79.0/24",
        "topology:",
        "  nodes:",
    ]
    for radio in data["radios"]:
        lines.extend(render_linux_node(data, radio["id"], radio["address"], f"radio --config /etc/containerlab-vrt/{radio['id']}.json"))
    lines.extend(render_linux_node(data, "processor", data["processor"]["address"], "processor --config /etc/containerlab-vrt/processor.json"))
    lines.extend(render_linux_node(data, "detector", data["detector"]["address"], "detector --config /etc/containerlab-vrt/detector.json"))
    lines.extend([
        "    recorder:",
        "      kind: linux",
        f"      image: {data['images']['application']}",
        f"      memory: {data['limits']['memory_bytes_per_application']}b",
        "      privileged: false",
        "      restart-policy: no",
        "      env:",
        f"        VRT_TELEMETRY_INTERVAL_MS: \"{data.get('telemetry', {}).get('interval_ms', 5000)}\"",
        "      cmd: recorder --interface eth1 --metrics /run/containerlab-vrt/recorder.json",
        "      cap-add:",
        "        - NET_ADMIN",
        "        - NET_RAW",
        "      exec:",
        f"        - ip link set dev eth1 mtu {data['application_mtu']}",
        "        - ip link set dev eth1 up",
        "    switch1:",
        f"      kind: {data['switch']['kind']}",
        f"      type: {data['switch']['type']}",
        f"      image: {data['images']['srlinux']}",
        "      startup-config: srlinux.cli",
        "  links:",
    ])
    endpoints = ["radio1", "radio2", "radio3", "radio4", "processor", "detector", "recorder"]
    for index, endpoint in enumerate(endpoints, 1):
        lines.append(f"    - endpoints: [\"switch1:ethernet-1/{index}\", \"{endpoint}:eth1\"]")
    return "\n".join(lines) + "\n"


def render_linux_node(data: dict[str, Any], name: str, address: str, command: str) -> list[str]:
    return [
        f"    {name}:",
        "      kind: linux",
        f"      image: {data['images']['application']}",
        f"      memory: {data['limits']['memory_bytes_per_application']}b",
        "      privileged: false",
        "      restart-policy: no",
        "      env:",
        f"        VRT_TELEMETRY_INTERVAL_MS: \"{data.get('telemetry', {}).get('interval_ms', 5000)}\"",
        "      cap-add:",
        "        - NET_ADMIN",
        f"      cmd: {command}",
        "      binds:",
        f"        - {name}.json:/etc/containerlab-vrt/{name}.json:ro",
        "      exec:",
        f"        - ip link set dev eth1 mtu {data['application_mtu']}",
        f"        - ip address replace {address} dev eth1",
        "        - ip link set dev eth1 up",
    ]


def render_srlinux(data: dict[str, Any]) -> str:
    lines = []
    for index in range(1, 8):
        lines.extend([
            f"set / interface ethernet-1/{index} admin-state enable",
            f"set / interface ethernet-1/{index} mtu 9014",
            f"set / interface ethernet-1/{index} subinterface 0 type {'local-mirror-dest' if index == 7 else 'bridged'}",
            f"set / interface ethernet-1/{index} subinterface 0 admin-state enable",
        ])
        if index < 7:
            lines.append(f"set / interface ethernet-1/{index} subinterface 0 l2-mtu 9000")
    lines.extend(["set / network-instance app type mac-vrf", "set / network-instance app admin-state enable"])
    for index in range(1, 7):
        lines.append(f"set / network-instance app interface ethernet-1/{index}.0")
    lines.extend([
        "set / interface ethernet-1/7 subinterface 0 local-mirror-destination admin-state enable",
        "set / system mirroring mirroring-instance capture admin-state enable",
        "set / system mirroring mirroring-instance capture mirror-destination local ethernet-1/7.0",
    ])
    for index in range(1, 7):
        lines.append(f"set / system mirroring mirroring-instance capture mirror-source interface ethernet-1/{index} direction ingress-only")
    return "\n".join(lines) + "\n"


def render_outputs(data: dict[str, Any]) -> dict[str, bytes]:
    processor_ip = str(ipaddress.ip_interface(data["processor"]["address"]).ip)
    detector_ip = str(ipaddress.ip_interface(data["detector"]["address"]).ip)
    outputs: dict[str, bytes] = {
        "four-radio.clab.yml": render_topology(data).encode(),
        "srlinux.cli": render_srlinux(data).encode(),
    }
    for radio in data["radios"]:
        outputs[f"{radio['id']}.json"] = canonical_json(app_config(data, radio)).encode()
    processor = {
        "schema": 1,
        "role": "processor",
        "application": {"address": data["processor"]["address"], "interface": "eth1", "mtu": data["application_mtu"]},
        "radios": [{
            "id": radio["id"], "sid": radio["sid"],
            "control": {"host": str(ipaddress.ip_interface(radio["address"]).ip), "port": radio["control_port"]},
            "iq": {"bind": "0.0.0.0", "port": radio["iq_port"]},
            "status": {"host": radio["id"], "port": radio["status_port"]},
            "settings": {
                "center_hz": data["radio_defaults"]["center_hz"],
                "sample_rate_hz": data["radio_defaults"]["sample_rate_hz"],
                "bandwidth_hz": data["radio_defaults"]["bandwidth_hz"],
                "gain_db_q7": data["radio_defaults"]["gain_db_q7"],
            },
        } for radio in data["radios"]],
        "spectrum": {**data["spectrum"], "destination": {"host": detector_ip, "port": data["detector"]["spectrum_port"]}},
        "limits": data["limits"],
    }
    outputs["processor.json"] = canonical_json(processor).encode()
    outputs["detector.json"] = canonical_json({
        "schema": 1, "role": "detector",
        "spectrum": {**data["spectrum"], "bind": "0.0.0.0", "port": data["detector"]["spectrum_port"]},
        "maximum_frequency_error_hz": 244.140625,
        "limits": data["limits"],
    }).encode()
    return outputs


def produce(source: Path, output: Path, check: bool) -> None:
    data = load_and_validate(source)
    outputs = render_outputs(data)
    source_hashes = {
        "application-parameters": digest(source.read_bytes()),
        "generator": digest(Path(__file__).read_bytes()),
    }
    manifest = {
        "schema": 1,
        "inputs": source_hashes,
        "outputs": {name: digest(content) for name, content in sorted(outputs.items())},
    }
    outputs["manifest.json"] = canonical_json(manifest).encode()
    if check:
        mismatches = [name for name, content in outputs.items() if not (output / name).is_file() or (output / name).read_bytes() != content]
        if mismatches:
            raise SystemExit("stale or missing generated files: " + ", ".join(mismatches))
        return
    output.mkdir(parents=True, exist_ok=True)
    for name, content in outputs.items():
        with tempfile.NamedTemporaryFile(dir=output, delete=False) as temporary:
            temporary.write(content)
            temporary_path = Path(temporary.name)
        os.replace(temporary_path, output / name)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    produce(arguments.source.resolve(), arguments.output.resolve(), arguments.check)


if __name__ == "__main__":
    main()
