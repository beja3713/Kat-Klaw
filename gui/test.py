import FreeSimpleGUI as sg
import os
from collections import defaultdict
import csv
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import subprocess
import pandas as pd
import numpy as np
import glob
import os
import serial
import time



# ------------------- Serial Setup -------------------
SERIAL_PORT = "/dev/ttyACM0"  # or COM3 on Windows
BAUD_RATE = 115200

try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
except Exception as e:
    sg.popup_error(f"Cannot open serial port {SERIAL_PORT}:\n{e}")
    ser = None

latest_gesture = ""


sg.set_options(
    background_color='white',       # window + frame background
    element_background_color='white',  # buttons, text, etc
    text_color='black'              # text
)

sg.theme_background_color('white')
sg.theme_text_color('black')
sg.theme_element_background_color('white')


# ------------------- Utility Functions -------------------


def flash_arduino():
    CLI = os.path.join("tools", "arduino-cli")
    PORT = "/dev/ttyACM0"   # COM3 on Windows
    FQBN = "esp32:esp32:adafruit_feather_esp32s3"
    SKETCH = os.path.join("sketches", "out_to_py")
    # SKETCH = "py_to_ino.py"
# 

    # Compile first, pointing to your bundled libraries
    compile_cmd = [
        CLI, "compile",
        "--fqbn", FQBN,
        SKETCH,
        "--libraries", os.path.join("sketches", "out_to_py", "libraries")
    ]

    result = subprocess.run(compile_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sg.popup_error("Compilation failed", result.stderr)
        return

    # If compilation succeeded, upload the compiled sketch
    upload_cmd = [
        CLI, "upload",
        "-p", PORT,
        "--fqbn", FQBN,
        SKETCH,
    ]

    result = subprocess.run(upload_cmd, capture_output=True, text=True)
    if result.returncode == 0:
        sg.popup("Upload successful!")
    else:
        sg.popup_error("Upload failed", result.stderr)

def flash_classifier():
    CLI = os.path.join("tools", "arduino-cli")
    PORT = "/dev/ttyACM0"
    FQBN = "esp32:esp32:adafruit_feather_esp32s3"
    SKETCH = os.path.join("sketches", "live_gesture_recognition_v2")  
    # folder that contains THIS .ino

    compile_cmd = [
        CLI, "compile",
        "--fqbn", FQBN,
        SKETCH,
        "--libraries", os.path.join("sketches", "live_gesture_recognition_v2", "libraries")
    ]

    result = subprocess.run(compile_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sg.popup_error("Compilation failed", result.stderr)
        return

    upload_cmd = [
        CLI, "upload",
        "-p", PORT,
        "--fqbn", FQBN,
        SKETCH
    ]

    result = subprocess.run(upload_cmd, capture_output=True, text=True)
    if result.returncode == 0:
        sg.popup("Classifier firmware uploaded!")
    else:
        sg.popup_error("Upload failed", result.stderr)

def read_arduino_full(ser):
    """
    Reads all available lines from Arduino without blocking.
    Returns a list of all lines read.
    """
    if not ser:
        return []

    lines = []
    while ser.in_waiting:
        try:
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                lines.append(line)
        except:
            continue
    return lines


def read_arduino(ser):
    """
    Reads lines from Arduino without blocking.
    Returns the last gesture detected as string.
    """
    if not ser:
        return None

    gesture = None
    while ser.in_waiting:
        try:
            line = ser.readline().decode(errors="ignore").strip()
        except:
            continue
        if line.startswith("Detected: "):
            gesture = line.replace("Detected: ", "")
    return gesture


import os

def generate_templates_index(templates_dir):
    """
    Generates templates_index.h using inline arrays, no linker issues.
    """
    headers = sorted(f for f in os.listdir(templates_dir) if f.endswith("_template.h"))
    out_path = os.path.join(templates_dir, "templates_index.h")

    with open(out_path, "w") as f:
        f.write("#pragma once\n\n")

        # Include all gesture headers
        for h in headers:
            f.write(f'#include "{h}"\n')
        f.write("\n")

        # Define gesture names inline
        f.write(f"inline constexpr int NUM_GESTURES = {len(headers)};\n\n")
        f.write("inline constexpr const char* gesture_names[NUM_GESTURES] = {\n")
        for h in headers:
            name = h.replace("_template.h", "").replace("_", " ").title()
            f.write(f'  "{name}",\n')
        f.write("};\n\n")

        # Define gesture data arrays inline
        f.write(f"inline constexpr const float (*gesture_data[NUM_GESTURES])[{AXES}] = {{\n")
        for h in headers:
            var = h.replace(".h", "")
            f.write(f"  {var},\n")
        f.write("};\n")

    print("Generated:", out_path)





def build_all_templates(project_folder):
    templates_dir = os.path.join(project_folder, "sketches", "live_gesture_recognition_v2", "templates")
    os.makedirs(templates_dir, exist_ok=True)

    gesture_groups = load_gesture_files(project_folder)

    for gesture in gesture_groups.keys():
        print("Averaging:", gesture)
        data = average_gesture(project_folder, gesture)
        if data:
            csv_path = os.path.join(project_folder, f"{gesture}_template.csv")
            generate_template_header(csv_path, templates_dir)

    # Generate index files (header + cpp)
    generate_templates_index(templates_dir)
    print("All templates built successfully.")




def load_gesture_files(project_folder):
    """
    Returns dict: gesture_name -> list of CSV files
    """
    gestures_dir = os.path.join(project_folder, "normalized_gestures")
    if not os.path.isdir(gestures_dir):
        return {}

    groups = defaultdict(list)
    for filename in os.listdir(gestures_dir):
        if filename.endswith(".csv"):
            prefix = filename.split("_")[0]
            groups[prefix].append(filename)
    return dict(groups)


def load_csv(path):
    data = []
    with open(path, newline='') as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            data.append([float(x) for x in row])
    return data


def draw_plot(canvas, data):
    for child in canvas.winfo_children():
        child.destroy()

    fig, ax = plt.subplots(figsize=(5, 4))

    # Skip first column (time)
    data = [row[1:] for row in data]
    data = list(zip(*data))  # transpose rows → columns

    labels = ["ax", "ay", "az", "gx", "gy", "gz"]

    for i, col in enumerate(data):
        ax.plot(col, label=labels[i])

    ax.set_title("Gesture Preview")
    ax.set_xlabel("Sample")
    ax.set_ylabel("Value")
    ax.legend()

    fig_canvas = FigureCanvasTkAgg(fig, canvas)
    fig_canvas.draw()
    fig_canvas.get_tk_widget().pack(fill="both", expand=True)
    plt.close(fig)


def clear_other_lists(selected_key, window, gesture_groups):
    for gesture in gesture_groups.keys():
        key = f"-LIST-{gesture}-"
        if key != selected_key:
            window[key].update(set_to_index=[])


def custom_folder_picker():
    layout = [
        [sg.Text("Select project folder:", font=("Helvetica", 12), background_color='white')],
        [sg.Input(key="-FOLDER-", enable_events=True, size=(50, 1)), sg.FolderBrowse()],
        [sg.HorizontalSeparator()],
        [sg.Button("OK"), sg.Button("Cancel")]
    ]

    window = sg.Window("Select Project Folder", layout, modal=True, finalize=True)

    folder_path = None
    while True:
        event, values = window.read()
        if event in (sg.WINDOW_CLOSED, "Cancel"):
            break
        elif event == "OK":
            folder_path = values["-FOLDER-"]
            if folder_path and os.path.isdir(os.path.join(folder_path, "normalized_gestures")):
                break
            else:
                sg.popup_error(
                    "Folder must contain a 'normalized_gestures' directory",
                    title="Invalid Folder"
                )
    window.close()
    return folder_path


# ------------------- Gesture Functions -------------------

def average_gesture(project_folder, gesture, target_samples=64):
    """
    Averages CSVs for a gesture and saves averaged CSV
    Returns data as list of lists
    """
    FOLDER = os.path.join(project_folder, "normalized_gestures")
    OUTPUT_FILE = os.path.join(project_folder, f"{gesture}_template.csv")

    files = sorted(glob.glob(os.path.join(FOLDER, f"{gesture}_*.csv")))

    if len(files) < 2:
        print(f"Need at least 2 recordings for '{gesture}'")
        return None

    all_values = []

    for f in files:
        try:
            df = pd.read_csv(f)
        except pd.errors.EmptyDataError:
            print("Skipping empty file:", f)
            continue

        t = df["t"].values
        values = df[["ax","ay","az","gx","gy","gz"]].values

        # Resample to fixed number of samples
        t_new = np.linspace(t[0], t[-1], target_samples)
        values_resampled = np.array([np.interp(t_new, t, values[:, i]) for i in range(values.shape[1])]).T

        all_values.append(values_resampled)

    if not all_values:
        return None

    stacked = np.stack(all_values, axis=0)
    mean_values = np.mean(stacked, axis=0)

    out = pd.DataFrame(
        np.column_stack([t_new, mean_values]),
        columns=["t","ax","ay","az","gx","gy","gz"]
    )

    out.to_csv(OUTPUT_FILE, index=False)
    return out.values.tolist()


def record_gesture(gesture_name, project_folder):
    import serial
    import csv
    import time
    import numpy as np
    import os
    from scipy.interpolate import interp1d

    PORT = "/dev/ttyACM0"
    BAUD = 115200

    GESTURE = gesture_name
    START_TIMEOUT = 5.0
    END_SILENCE = 0.5
    N_SAMPLES = 100
    TARGET_DURATION = 1.0

    OUTPUT_FOLDER = os.path.join(project_folder, "normalized_gestures")
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)

    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.05)
    except Exception as e:
        sg.popup_error(
            "Device not connected\n\n"
            f"Could not open {PORT}\n\n"
            f"{e}"
        )
        return

    print("Waiting for serial data...")
    # ------------------ Add Popup for Waiting ------------------
    waiting_popup = sg.Window(
        "Waiting...",
        [[sg.Text("Waiting for Data...", key="-TXT-")]],
        modal=True,
        finalize=True
    )

    wait_start = time.time()
    rows = []

    # wait for start
    while True:
        if time.time() - wait_start > START_TIMEOUT:
            waiting_popup.close()
            raise TimeoutError("No serial data received")

        line = ser.readline().decode(errors="ignore").strip()
        if line.count(",") == 5:
            gesture_start = time.time()
            last_data_time = gesture_start
            rows.append([0.0] + line.split(","))

            # Close the waiting popup as soon as first data line is received
            waiting_popup.close()
            break

    wait_start = time.time()
    rows = []

    # wait for start
    while True:
        if time.time() - wait_start > START_TIMEOUT:
            raise TimeoutError("No serial data received")

        line = ser.readline().decode(errors="ignore").strip()
        if line.count(",") == 5:
            gesture_start = time.time()
            last_data_time = gesture_start
            rows.append([0.0] + line.split(","))
            break

    # record
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        now = time.time()

        if line.count(",") == 5:
            t = now - gesture_start
            rows.append([t] + line.split(","))
            last_data_time = now
        elif now - last_data_time > END_SILENCE:
            break

    data = np.array(rows)
    t_raw = data[:, 0].astype(float)
    values = data[:, 1:].astype(float)

    t_new = np.linspace(0, TARGET_DURATION, N_SAMPLES)
    f = interp1d(t_raw, values, axis=0, fill_value="extrapolate")
    values_new = f(t_new)

    filename = os.path.join(
        OUTPUT_FOLDER,
        f"{GESTURE}_{int(time.time())}.csv"
    )

    with open(filename, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["t", "ax", "ay", "az", "gx", "gy", "gz"])
        for i, row in enumerate(values_new):
            writer.writerow([t_new[i]] + row.tolist())

    print("Saved:", filename)

N_SAMPLES = 64
AXES = 6

def generate_template_header(csv_path, out_dir):
    """
    Converts averaged CSV to C++ header file using inline arrays.
    """
    df = pd.read_csv(csv_path)
    name = os.path.basename(csv_path).replace("_template.csv", "")
    header_name = f"{name}_template.h"
    out_path = os.path.join(out_dir, header_name)

    with open(out_path, "w") as f:
        f.write("#pragma once\n\n")
        f.write(f"inline constexpr float {name}_template[{N_SAMPLES}][{AXES}] = {{\n")

        for _, row in df.iterrows():
            vals = ", ".join(f"{v:.6f}f" for v in row[1:])
            f.write(f"  {{ {vals} }},\n")

        f.write("};\n")

    print("Generated:", out_path)



# ------------------- GUI Windows -------------------

def start_window():
    layout = [
        [sg.Button("New Project", size=(40, 20), button_color=('#e9a41b', '#000000')),
         sg.Button("Open Project", size=(40, 20), button_color=('#e9a41b', '#000000'))]
    ]
    return sg.Window("Kat Klaw Gesture Hub", layout)


def popup_plot(data, title="Averaged Gesture"):
    layout = [
        [sg.Text(title, font=("Helvetica", 14), background_color='white')],
        [sg.Canvas(key="-POP_CANVAS-", size=(500, 350))],
        [sg.Button("Close")]
    ]

    win = sg.Window(title, layout, modal=True, finalize=True)

    draw_plot(win["-POP_CANVAS-"].TKCanvas, data)

    while True:
        event, _ = win.read()
        if event in (sg.WINDOW_CLOSED, "Close"):
            break

    win.close()


def project_window(folder):
    gesture_groups = load_gesture_files(folder)
    gesture_labels = sorted(gesture_groups.keys())
    gesture_labels.append("<New...>")

    # Left column
    left_column = []
    for gesture, files in sorted(gesture_groups.items()):
        left_column.append([   # <-- wrap Frame in a list
            sg.Frame(
                gesture.upper(),
                [[sg.Listbox(
                    values=files,
                    size=(30, 8),
                    key=f"-LIST-{gesture}-",
                    enable_events=True,
                    background_color='white',
                    text_color='black'
                )]],
                background_color='white',
                title_color='black'
            )
        ])


    right_column = [
    [sg.Text("Preview", font=("Helvetica", 14), background_color='white')],
    [sg.Canvas(key="-CANVAS-", size=(400, 300))],
    [
        sg.Combo(gesture_labels, default_value=gesture_labels[0] if gesture_labels else '',
                 key="-GESTURE-", size=(10, 1), enable_events=True),
        sg.Button("Record", key="-ASSIGN-"),
        sg.Button("View output", key="-BTN2-"),
        # sg.Button("Upload to Klaw", key="-BTN3-")
    ],
    [
    sg.Button("Flash Klaw Trainer", key="-FLASH-"),
    sg.Button("Flash Klaw Classifier", key="-FLASH-CLS-")
    ],   
    [sg.Text("Latest Gesture:", key="-LIVE-GESTURE-", font=("Helvetica", 16), background_color='white')],
    [sg.Multiline("", size=(40, 10), key="-SERIAL-", autoscroll=True, disabled=True)],
]


    layout = [
        [sg.Text("Opened Project Folder:", background_color='white')],
        [sg.Text(folder, size=(80, 1), background_color='white')],
        [sg.HorizontalSeparator()],
        [
            sg.Column(left_column, scrollable=True, vertical_scroll_only=True, key="-LEFTCOL-"),
            sg.VSeparator(),
            sg.Column(right_column)
        ],
        [sg.Button("Back"), sg.Button("Delete")]
    ]

    return sg.Window("Project", layout, resizable=True, finalize=True)


# ------------------- Main Loop -------------------

# ------------------- Main Loop -------------------

folder = None
window = start_window()

while True:
    event, values = window.read(timeout=50)

    # # ------------------- Live Serial Read (Safe) -------------------
    # gesture_from_arduino = read_arduino(ser)
    # if gesture_from_arduino and window:                  # window must exist
    #     # Make sure window is not closed
    #     if "-LIVE-GESTURE-" in window.AllKeysDict:
    #         try:
    #             window["-LIVE-GESTURE-"].update(gesture_from_arduino)
    #         except sg.tk.TclError:
    #             # Window closed, ignore
    #             pass

    # ------------------- Live Serial Read (Full) -------------------
    lines = read_arduino_full(ser)
    if lines and window:
        # Append all lines to multiline
        if "-SERIAL-" in window.AllKeysDict:
            try:
                for line in lines:
                    window["-SERIAL-"].print(line)
            except sg.tk.TclError:
                pass

        # Update latest gesture if line starts with "Detected: "
        for line in lines:
            if line.startswith("Detected: "):
                gesture = line.replace("Detected: ", "")
                if "-LIVE-GESTURE-" in window.AllKeysDict:
                    try:
                        window["-LIVE-GESTURE-"].update(gesture)
                    except sg.tk.TclError:
                        pass


    # ------------------ Start Window ------------------
    if event == sg.WINDOW_CLOSED:
        break
    elif event == "Open Project":
        folder = custom_folder_picker()
        if folder:
            window.close()
            window = project_window(folder)

    elif event == "Back":
        folder = None
        window.close()
        window = start_window()

    # ------------------ Delete Gesture ------------------
    elif event == "Delete" and folder:
        gesture_groups = load_gesture_files(folder)
        deleted = False
        for gesture, files in gesture_groups.items():
            listbox_key = f"-LIST-{gesture}-"
            highlighted = window[listbox_key].get()
            if highlighted:
                csv_file = highlighted[0]
                full_path = os.path.join(folder, "normalized_gestures", csv_file)
                confirm = sg.popup_yes_no(
                    f"Are you sure you want to delete {csv_file}?",
                    background_color='white'
                )
                if confirm == 'Yes':
                    os.remove(full_path)
                    deleted = True
                break

        # Rebuild window if something was deleted
        if deleted:
            window.close()
            window = project_window(folder)

    # ------------------ Record Gesture ------------------
    elif event == "-GESTURE-" and folder:
        if values["-GESTURE-"] == "<New...>":
            new_label = sg.popup_get_text("Enter new gesture label:")
            if new_label:
                new_label = new_label.strip().lower()
                gesture_groups = load_gesture_files(folder)

                if new_label in gesture_groups:
                    sg.popup("Gesture already exists")
                else:
                    # Add the new label to the combo (no placeholder CSV needed)
                    labels = sorted(list(gesture_groups.keys()) + [new_label]) + ["<New...>"]
                    window["-GESTURE-"].update(values=labels, value=new_label)



    # ------------------ Average / View Gesture ------------------
    elif event == "-BTN2-" and folder:
        gesture = values["-GESTURE-"]
        data = average_gesture(folder, gesture)

        if data:
            csv_path = os.path.join(folder, f"{gesture}_template.csv")
            generate_template_header(
                csv_path,
                "sketches/live_gesture_recognition_v2/templates"
            )
            popup_plot(data, f"{gesture.upper()} Template")

    # ------------------ Flash Firmware ------------------
    elif event == "-FLASH-" and folder:
        print("Flashing Klaw firmware...")
        if ser and ser.is_open:
            try:
                ser.close()
            except:
                pass
        flash_arduino()
        time.sleep(2)

        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
        except Exception as e:
            sg.popup_error(f"Could not reopen serial port:\n{e}") 

        sg.popup("Firmware flash complete!")

    elif event == "-FLASH-CLS-" and folder:
        build_all_templates(folder)
        print("Flashing classifier firmware...")
        if ser and ser.is_open:
            try:
                ser.close()
            except:
                pass
        flash_classifier()

        time.sleep(2)

        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
        except Exception as e:
            sg.popup_error(f"Could not reopen serial port:\n{e}") 


    # ------------------ Add New Gesture ------------------
    elif event == "-ASSIGN-" and folder:
        gesture = values["-GESTURE-"]
        if not gesture:
            sg.popup("Select a gesture first")
            continue

        port = "/dev/ttyACM0"   # or COM3, hardcoded

        # if not is_correct_firmware(port):
        #     print("Flashing with streaming code")
        #     flash_arduino()
        # else:
        #     print("Streaming code is already flashed")
        


        # Check if this gesture previously had no CSVs
        gesture_groups = load_gesture_files(folder)
        was_empty = gesture not in gesture_groups or len(gesture_groups[gesture]) == 0

        # Record the gesture
        record_gesture(gesture, folder)

        # Refresh window ONLY if this was the first CSV for a new label
        if was_empty:
            window.close()
            window = project_window(folder)
            window["-GESTURE-"].update(value=gesture)
        else:
            # Otherwise, just update the listbox for this gesture
            gesture_groups = load_gesture_files(folder)
            listbox_key = f"-LIST-{gesture}-"
            if listbox_key in window.AllKeysDict:
                window[listbox_key].update(values=gesture_groups.get(gesture, []))




    # ------------------ Listbox Selection ------------------
    elif isinstance(event, str) and event.startswith("-LIST-") and folder:
        selected = values[event]
        if not selected:
            continue
        gesture_groups = load_gesture_files(folder)
        # Clear other listboxes
        for gesture in gesture_groups.keys():
            key = f"-LIST-{gesture}-"
            if key != event:
                window[key].update(set_to_index=[])
        # Draw plot for selected CSV
        filename = selected[0]
        csv_path = os.path.join(folder, "normalized_gestures", filename)
        data = load_csv(csv_path)
        draw_plot(window["-CANVAS-"].TKCanvas, data)

window.close()

