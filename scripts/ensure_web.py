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

Import("env")

from pathlib import Path
import shutil
import subprocess


project_dir = Path(env.subst("$PROJECT_DIR"))
web_html = project_dir / "src" / "web" / "provisioning.html"
web_gzip = project_dir / "src" / "web" / "provisioning.html.gz"

npm = shutil.which("npm")
if npm is None:
    raise RuntimeError("Install Node.js/npm and run npm ci before building with PlatformIO")

result = subprocess.run([npm, "run", "build:web"], cwd=project_dir, check=False)
if result.returncode != 0 or not web_html.exists() or not web_gzip.exists():
    raise RuntimeError("Could not generate provisioning HTML assets; run npm ci")
