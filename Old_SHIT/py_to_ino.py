import pandas as pd
import sys

# ---------- CONFIG ----------
gesture = sys.argv[1]
csv_file = f"{gesture}_template.csv"  # your CSV
output_file = f"{gesture}_template.h"
array_name = f"{gesture}_template"

# ---------- LOAD DATA ----------
df = pd.read_csv(csv_file)

# Keep only the IMU columns
cols = ["ax", "ay", "az", "gx", "gy", "gz"]
data = df[cols].values

# ---------- WRITE TO FILE ----------
with open(output_file, "w") as f:
    f.write(f"const float {array_name}[{len(data)}][6] = {{\n")
    for row in data:
        row_str = ", ".join(f"{v:.6f}f" for v in row)
        f.write(f"  {{{row_str}}},\n")
    f.write("};\n")

print(f"Arduino template written to {output_file}")
