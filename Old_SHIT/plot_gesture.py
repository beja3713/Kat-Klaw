import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
import sys

matplotlib.use("TkAgg")

if len(sys.argv) < 2:
    print("Usage: python plot_gesture.py file.csv")
    sys.exit(1)

df = pd.read_csv(sys.argv[1])

t = df["t"]

axes = ["ax", "ay", "az", "gx", "gy", "gz"]

plt.figure(figsize=(10, 6))

for a in axes:
    plt.plot(t, df[a], label=a)

plt.xlabel("Time (s)")
plt.ylabel("IMU value")
plt.title(sys.argv[1])
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()
