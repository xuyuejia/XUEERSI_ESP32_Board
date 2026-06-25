"""Build + Flash + Monitor for XRS LVGL Demo"""
import os
import sys
import subprocess

os.environ['IDF_PATH'] = r'C:\Users\ThinkPad\esp-idf-v5.4'

project_dir = r'C:\Users\ThinkPad\WorkBuddy\2026-06-25-16-58-51\xrs_lvgl_demo'
idf_py = os.path.join(os.environ['IDF_PATH'], 'tools', 'idf.py')

os.chdir(project_dir)

def run(args):
    cmd = [sys.executable, idf_py] + args
    print(f"\n=== {' '.join(cmd)} ===")
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print(f"FAILED: {args}")
        sys.exit(r.returncode)

run(['build'])
run(['-p', 'COM14', 'flash'])
run(['-p', 'COM14', 'monitor'])
