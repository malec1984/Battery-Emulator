Import("env")
import subprocess
import sys
import os

script_path = os.path.join(env["PROJECT_DIR"], "tools", "generate_meb_dtc_bin.py")
result = subprocess.run([sys.executable, script_path], cwd=env["PROJECT_DIR"])
if result.returncode != 0:
    print("WARNING: Failed to generate meb_dtc.bin")
