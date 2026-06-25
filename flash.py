"""Flash the built firmware to XRS Coding Machine"""
import os
import sys
import subprocess

os.environ['IDF_PATH'] = r'C:\Users\ThinkPad\esp-idf-v5.4'

project_dir = r'C:\Users\ThinkPad\WorkBuddy\2026-06-25-16-58-51\xrs_lvgl_demo'
idf_py = os.path.join(os.environ['IDF_PATH'], 'tools', 'idf.py')

os.chdir(project_dir)

cmd = [sys.executable, idf_py, '-p', 'COM14', 'flash']
print(f"Running: {' '.join(cmd)}")
result = subprocess.run(cmd)
sys.exit(result.returncode)
