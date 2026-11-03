"""Compatibility import; shared Unity implementation lives in lib.unity."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.unity.unity_scene import *  # noqa: F401,F403
