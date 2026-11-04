# Point d’entrée PlatformIO (extra_scripts). La logique est dans web_assets.py,
# importable, et testée par tests/test_web_assets.py.
import os
import sys

Import("env")

PROJECT_DIR = env.subst("$PROJECT_DIR")
sys.path.insert(0, os.path.join(PROJECT_DIR, "tools"))
from web_assets import firmware_version, generate

generate(PROJECT_DIR, firmware_version(env))
