#!/usr/bin/env python3
import subprocess
import os
import sys

# Get paths
cwd = os.getcwd()
exe_path = os.path.join(cwd, 'build', 'powercapslock.exe')
mp3_path = os.path.join(cwd, 'testcase', 'testcase_1.mp3')
txt_path = os.path.join(cwd, 'testcase', 'testcase_1.txt')

print(f"Running: {exe_path} --enable-voice --test-audio-file {mp3_path} {txt_path}")
print()

# Run with output captured as bytes
result = subprocess.run([exe_path, '--enable-voice', '--test-audio-file', mp3_path, txt_path],
                       capture_output=True)

print(f"Exit code: {result.returncode}")
print()

print("STDOUT:")
try:
    # Decode
    text = result.stdout.decode('utf-8')
    # Replace emoji with ASCII
    text = text.replace('\u2705', '[PASS]').replace('\u274c', '[FAIL]')
    print(text)
except UnicodeDecodeError:
    text = result.stdout.decode('gbk')
    text = text.replace('✅', '[PASS]').replace('❌', '[FAIL]')
    print(text)
print()

print("STDERR:")
try:
    text = result.stderr.decode('utf-8')
    text = text.replace('\u2705', '[PASS]').replace('\u274c', '[FAIL]')
    print(text)
except UnicodeDecodeError:
    text = result.stderr.decode('gbk')
    text = text.replace('✅', '[PASS]').replace('❌', '[FAIL]')
    print(text)
