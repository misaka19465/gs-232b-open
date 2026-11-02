#!/usr/bin/env python3
# GS-232B Open - rotator controller firmware.
# Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Measure fixed allocations with the built firmware's compiler and ESP32-C3 ABI."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "esp32-c3-wroom-02u-n4"


def measure(environment: str) -> dict:
    """Compile an unlinked layout probe and read symbol sizes from the target ELF."""
    build = ROOT / ".pio" / "build" / environment
    description = json.loads((build / "project_description.json").read_text())
    if description["target"] != "esp32c3" or description["git_revision"] != "6.1.0":
        raise RuntimeError("Revalidate resource budgets for this SDK or target")
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(item for item in entries if item["file"].endswith("/app/esp_app.c"))
    command = entry.get("arguments") or shlex.split(entry["command"])
    compiler = Path(command[0])
    flags: list[str] = []
    skip = False
    for argument in command[1:]:
        if skip:
            skip = False
        elif argument in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif argument not in ("-c", "-MD", "-MMD", "-MP", entry["file"]):
            flags.append(argument)
    output = ROOT / "build" / "resource-budget"
    output.mkdir(parents=True, exist_ok=True)
    probe = output / "resource_layout.o"
    subprocess.run([str(compiler), *flags, "-c", str(ROOT / "tests/budget/resource_layout.c"),
                    "-o", str(probe)], cwd=entry["directory"], check=True)
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")

    def symbols(path: Path) -> dict[str, int]:
        result = subprocess.check_output([str(nm), "-S", "--defined-only", str(path)], text=True)
        return {parts[3]: int(parts[1], 16) for line in result.splitlines()
                if len(parts := line.split()) == 4}

    sizes = {name.removeprefix("gs232b_budget_"): value for name, value in symbols(probe).items()
             if name.startswith("gs232b_budget_")}
    firmware = symbols(build / "firmware.elf")
    runtime = firmware.get("runtime")
    if runtime != sizes["runtime"]:
        raise RuntimeError(f"Firmware runtime {runtime} differs from probe {sizes['runtime']}; rebuild")
    return {"environment": environment, "target": description["target"],
            "idf_version": description["git_revision"], "bytes": sizes,
            "runtime_elf_bytes": runtime,
            "board_heap_peak": None, "board_control_latency_us": None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment", default=ENVIRONMENT)
    parser.add_argument("--output", type=Path, default=ROOT / "build/resource-budget/budget.json")
    args = parser.parse_args()
    report = measure(args.environment)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
