#!/usr/bin/env python3
"""
Analyze the screenshot to check if controls are visible
"""

from PIL import Image
import sys

def analyze_screenshot(image_path):
    img = Image.open(image_path)
    print(f"Image size: {img.width}x{img.height}, mode: {img.mode}")

    # Convert to RGB
    img = img.convert('RGB')

    # Check the general areas
    print("\n=== Region Analysis ===")

    # Expected regions:
    # Top: tabs (we already know they show text)
    # Middle: content area - should not be all the same color

    # General content area (after tabs, before buttons at bottom)
    content_left = 10
    content_top = 50
    content_right = img.width - 10
    content_bottom = img.height - 40

    print(f"\nContent area: {content_left},{content_top} to {content_right},{content_bottom}")

    # Sample some pixels to check if there's variation (not blank)
    pixels = []
    for y in range(content_top, content_bottom, 20):
        for x in range(content_left, content_right, 40):
            r, g, b = img.getpixel((x, y))
            pixels.append((r, g, b))

    unique_colors = len(set(pixels))
    print(f"Sampled {len(pixels)} pixels, found {unique_colors} unique colors")

    if unique_colors < 10:
        print("WARNING: Content area appears to be blank/uniform")
    else:
        print("OK: Content area has varied pixels - controls are visible")

    # Check Log Directory region
    print("\n--- General Tab (top-left content area) ---")

    # Log Directory label should be around here
    log_label_y = content_top + 10
    log_label_x = content_left + 10
    print(f"Expected 'Log Directory:' label near {log_label_x},{log_label_y}")

    # Log Directory input box
    log_input_x = content_left + 90
    log_input_y = content_top + 5
    log_input_w = content_right - content_left - 160
    log_input_h = 22
    print(f"Log path input box: {log_input_x},{log_input_y} size {log_input_w}x{log_input_h}")

    # Browse button for log
    log_btn_x = content_right - 70
    log_btn_y = content_top + 5
    print(f"Browse button near {log_btn_x},{log_btn_y}")

    # Model Directory
    model_y = content_top + 35
    print(f"Model Directory label near {content_left + 10},{model_y + 10}")
    print(f"Model path input box: {log_input_x},{model_y} size {log_input_w}x{log_input_h}")
    print(f"Browse button near {log_btn_x},{model_y + 5}")

    print("\n--- Key Mappings Tab ---")
    print("List view should fill most of the content area on the left")
    print("Four buttons should be stacked vertically on the right")

    # Check if any non-background pixels
    bg_color = img.getpixel((content_left + 5, content_top + 5))
    diff_count = 0
    for y in range(content_top, content_bottom):
        for x in range(content_left, content_right):
            pixel = img.getpixel((x, y))
            if pixel != bg_color:
                diff_count += 1
    print(f"Found {diff_count} pixels different from top-left background pixel")

    if diff_count > (content_right - content_left) * (content_bottom - content_top) * 0.05:
        print("OK: More than 5% of content area has non-background pixels - content visible")
    else:
        print("WARNING: Less than 5% has non-background - likely still blank")

    return True

if __name__ == "__main__":
    image_path = "config_dialog_screenshot.png"
    analyze_screenshot(image_path)
