import serial
import csv
import time
import numpy as np
import os
import sys
from scipy.interpolate import interp1d

PORT = "/dev/ttyACM0"
BAUD = 115200

# GESTURE = "down_swipe"
GESTURE = sys.argv[1]
START_TIMEOUT = 5.0
END_SILENCE = 0.5
N_SAMPLES = 100       # resample to 100 samples
TARGET_DURATION = 1.0 # normalize to 1 second
OUTPUT_FOLDER = "normalized_gestures"

os.makedirs(OUTPUT_FOLDER, exist_ok=True)

ser = serial.Serial(PORT, BAUD, timeout=0.05)

print("Waiting for serial data...")

wait_start = time.time()
gesture_start = None
last_data_time = None
rows = []

# ---------- wait for gesture start ----------
while True:
    if time.time() - wait_start > START_TIMEOUT:
        raise TimeoutError("No serial data received")

    line = ser.readline().decode(errors="ignore").strip()
    if line.count(",") == 5:
        gesture_start = time.time()
        last_data_time = gesture_start
        t = 0.0
        rows.append([t] + line.split(","))
        print("Gesture started!")
        break

# ---------- record until silence ----------
while True:
    line = ser.readline().decode(errors="ignore").strip()
    now = time.time()

    if line.count(",") == 5:
        t = now - gesture_start
        rows.append([t] + line.split(","))
        last_data_time = now
    elif now - last_data_time > END_SILENCE:
        gesture_end = now
        print("Gesture ended")
        break

duration = gesture_end - gesture_start
print(f"Gesture duration: {duration:.3f} s")
print(f"Raw samples recorded: {len(rows)}")

# ---------- convert to numpy array ----------
data = np.array(rows)  # shape (num_samples, 7), first column is t
t_raw = data[:, 0].astype(float)
values = data[:, 1:].astype(float)

# ---------- normalize to TARGET_DURATION ----------
t_new = np.linspace(0, TARGET_DURATION, N_SAMPLES)
f = interp1d(t_raw, values, axis=0, kind="linear", fill_value="extrapolate")
values_new = f(t_new)

# ---------- save normalized CSV ----------
filename = os.path.join(OUTPUT_FOLDER, f"{GESTURE}_{int(time.time())}.csv")
with open(filename, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["t", "ax", "ay", "az", "gx", "gy", "gz"])
    for i, row in enumerate(values_new):
        writer.writerow([t_new[i]] + row.tolist())

print(f"Saved normalized gesture → {filename}")
