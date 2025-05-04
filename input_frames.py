import os
import subprocess
import math

# Configuration
input_y4m = "output_720p_20s.y4m"
output_dir = "./input_frames/"
width = 1280        # <-- Set your video width here
height = 720       # <-- Set your video height here
frame_rate = 25    # Optional, for sanity check
pix_fmt = "yuv420p"

# Derived parameters
frame_size = int(width * height * 3 / 2)  # YUV420p = 1.5 bytes per pixel
os.makedirs(output_dir, exist_ok=True)

# Extract rawvideo from .y4m
raw_yuv_path = os.path.join(output_dir, "full_output.yuv")
subprocess.run([
    "ffmpeg", "-y",
    "-i", input_y4m,
    "-f", "rawvideo",
    "-pix_fmt", pix_fmt,
    raw_yuv_path
], check=True)

# Split into per-frame files
with open(raw_yuv_path, "rb") as f:
    frame_idx = 0
    while True:
        data = f.read(frame_size)
        if not data or len(data) < frame_size:
            break
        frame_path = os.path.join(output_dir, f"frame_{frame_idx:04d}.yuv")
        with open(frame_path, "wb") as out:
            out.write(data)
        print(f"Saved frame {frame_idx}")
        frame_idx += 1

print(f"Extracted {frame_idx} frames to: {output_dir}")