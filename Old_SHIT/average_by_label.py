import pandas as pd
import numpy as np
import glob
import sys
import os

FOLDER = "normalized_gestures"
# GESTURE = "up"
GESTURE = sys.argv[1]
OUTPUT_FILE = f"{GESTURE}_template.csv"

# ---------- find gesture files ----------
files = sorted(glob.glob(os.path.join(FOLDER, f"{GESTURE}_*.csv")))

if len(files) < 2:
    raise RuntimeError("Need at least 2 files to average")

print(f"Found {len(files)} files")

# ---------- load all gestures ----------
all_data = []
t_ref = None

for f in files:
    df = pd.read_csv(f)

    # separate time and values
    t = df["t"].values
    values = df[["ax","ay","az","gx","gy","gz"]].values

    # ensure same time base
    if t_ref is None:
        t_ref = t
    else:
        if len(t) != len(t_ref):
            raise ValueError("Sample count mismatch")

    all_data.append(values)

# ---------- average ----------
stacked = np.stack(all_data, axis=0)  # shape (num_files, N, 6)
mean_values = np.mean(stacked, axis=0)

# ---------- save template ----------
out = pd.DataFrame(
    np.column_stack([t_ref, mean_values]),
    columns=["t","ax","ay","az","gx","gy","gz"]
)

out.to_csv(OUTPUT_FILE, index=False)

print(f"Saved averaged template → {OUTPUT_FILE}")
