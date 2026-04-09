#!/usr/bin/env python3
"""
Test script to launch PowerCapslock config dialog and capture screenshot for verification
"""

import subprocess
import time
import os
import sys

try:
    import win32gui
    import win32ui
    from PIL import Image
    import win32con
except ImportError:
    print("ERROR: Required packages not installed. Please install:")
    print("  pip install pywin32 Pillow")
    sys.exit(1)

def capture_window(window_title):
    """Capture screenshot of a window by title"""
    # Find the window
    hwnd = win32gui.FindWindow(None, window_title)
    if hwnd == 0:
        # Try partial match
        def callback(handle, extra):
            if window_title.lower() in win32gui.GetWindowText(handle).lower():
                extra.append(handle)
            return True
        handles = []
        win32gui.EnumWindows(callback, handles)
        if not handles:
            print(f"ERROR: Window '{window_title}' not found")
            return None
        hwnd = handles[0]

    # Get window dimensions
    left, top, right, bottom = win32gui.GetWindowRect(hwnd)
    width = right - left
    height = bottom - top

    print(f"Found window: {win32gui.GetWindowText(hwnd)} at {left},{top} {width}x{height}")

    # Bring window to foreground
    win32gui.SetForegroundWindow(hwnd)
    time.sleep(0.5)

    # Capture screenshot
    hdc = win32gui.GetWindowDC(hwnd)
    dcobj = win32ui.CreateDCFromHandle(hdc)
    memdc = dcobj.CreateCompatibleDC()

    bmp = win32ui.CreateBitmap()
    bmp.CreateCompatibleBitmap(dcobj, width, height)
    memdc.SelectObject(bmp)
    memdc.BitBlt((0, 0), (width, height), dcobj, (0, 0), win32con.SRCCOPY)

    # Convert to PIL Image
    bmpinfo = bmp.GetInfo()
    bmpbytes = bmp.GetBitmapBits(True)
    img = Image.frombuffer(
        'RGB',
        (bmpinfo['bmWidth'], bmpinfo['bmHeight']),
        bmpbytes, 'raw', 'BGRX', 0, 1)

    # Cleanup
    memdc.DeleteDC()
    dcobj.DeleteDC()
    win32gui.ReleaseDC(hwnd, hdc)
    win32gui.DeleteObject(bmp.GetHandle())

    return img

def main():
    # Kill any existing process
    subprocess.run(['taskkill', '/F', '/IM', 'powercapslock.exe'],
                   capture_output=True, check=False)
    time.sleep(1)

    # Start the program with config dialog test
    exe_path = os.path.join(os.getcwd(), 'build', 'powercapslock.exe')
    print(f"Starting: {exe_path} --test-config-dialog")

    proc = subprocess.Popen([exe_path, '--test-config-dialog'])
    print(f"Started process PID: {proc.pid}")

    # Wait for window to appear
    time.sleep(2)

    # Try to capture the main config dialog
    window_title = "PowerCapslock Configuration"
    img = capture_window(window_title)

    if img is not None:
        output_file = "config_dialog_screenshot.png"
        img.save(output_file)
        print(f"Screenshot saved to: {output_file}")
        print(f"Image size: {img.width}x{img.height}")
    else:
        print("Failed to capture screenshot")
        proc.terminate()
        return 1

    print("\nTest completed! Check the screenshot file.")
    print("You can manually close the dialog window.")

    return 0

if __name__ == "__main__":
    sys.exit(main())
