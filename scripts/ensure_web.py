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
