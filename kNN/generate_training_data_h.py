#!/usr/bin/env python3
import csv
import random
import re
from pathlib import Path
import pandas as pd

INPUT_FILE = Path("dataML_training.csv")
OUTPUT_FILE = Path("include/training_data.h")
MAX_TRAINING_COUNT = 200
RANDOM_SEED = 42

IDENTIFIER_PATTERN = re.compile(r"[^A-Za-z0-9_]")

# Các ngưỡng an toàn tham khảo cho bảo quản/vận chuyển đạn dược
SAFE_PROFILES = [
    {
        "name": "infantry_ammo",
        "temperature": (10.0, 25.0),
        "humidity": (30.0, 65.0),
        "accel_magnitude": (0.0, 12.0),
        "note": "Ammunition storage and transport safe range",
    },
    {
        "name": "explosives",
        "temperature": (10.0, 40.0),
        "humidity": (0.0, 60.0),
        "accel_magnitude": (0.0, 10.0),
        "note": "Explosives/propellant safe transport",
    },
    {
        "name": "smart_munitions",
        "temperature": (-20.0, 40.0),
        "humidity": (40.0, 50.0),
        "accel_magnitude": (0.0, 8.0),
        "note": "Smart munitions sensitive electronics transport",
    },
]

JITTER_SCALE = {
    "temperature": 0.5,
    "humidity": 1.0,
    "accel_magnitude": 0.5,
}


def clip(value, min_value, max_value):
    return max(min_value, min(max_value, value))


def random_safe_value(value, bounds, scale):
    min_value, max_value = bounds
    if value < min_value or value > max_value:
        return random.uniform(min_value, max_value)
    return clip(value + random.uniform(-scale, scale), min_value, max_value)


def sanitize_identifier(name: str) -> str:
    name = name.strip().replace(" ", "_")
    name = IDENTIFIER_PATTERN.sub("", name)
    if not name:
        name = "VEHICLE"
    if name[0].isdigit():
        name = "V" + name
    return name


def jitter_packet(packet):
    profile = random.choice(SAFE_PROFILES)
    return {
        "temperature": random_safe_value(packet["temperature"], profile["temperature"], JITTER_SCALE["temperature"]),
        "humidity": random_safe_value(packet["humidity"], profile["humidity"], JITTER_SCALE["humidity"]),
        "accel_magnitude": random_safe_value(packet["accel_magnitude"], profile["accel_magnitude"], JITTER_SCALE["accel_magnitude"]),
    }


def format_packet(packet):
    return "    {{{:.2f}, {:.2f}, {:.2f}}},".format(
        packet["temperature"], packet["humidity"], packet["accel_magnitude"]
    )


def load_training_csv(path: Path):
    records = []
    df = pd.read_csv(path)
    
    # Map column names
    cols = {c.lower(): c for c in df.columns}
    
    vehicle_col = next((cols[k] for k in ['vehicle_id', 'transport_id', 'device_id'] if k in cols), None)
    temp_col = next((cols[k] for k in ['temperature', 'temp'] if k in cols), None)
    humid_col = next((cols[k] for k in ['humidity', 'h'] if k in cols), None)
    accel_col = next((cols[k] for k in ['accel_magnitude', 'a_g'] if k in cols), None)
    label_col = next((cols[k] for k in ['label', 'ml_prediction'] if k in cols), None)
    anom_type_col = cols.get('anomaly_type')
    anom_reason_col = cols.get('anomaly_reason')
    
    for _, row in df.iterrows():
        try:
            records.append({
                "vehicle_id": str(row[vehicle_col]).strip() if vehicle_col else "unknown",
                "temperature": float(row[temp_col]) if temp_col else 25.0,
                "humidity": float(row[humid_col]) if humid_col else 50.0,
                "accel_magnitude": float(row[accel_col]) if accel_col else 1.0,
                "label": str(row[label_col]).strip().upper() if label_col else "NORMAL",
                "anomaly_type": str(row[anom_type_col]).strip() if anom_type_col else "",
                "anomaly_reason": str(row[anom_reason_col]).strip() if anom_reason_col else "",
            })
        except (ValueError, TypeError):
            continue
    return pd.DataFrame(records)


def main():
    if not INPUT_FILE.exists():
        raise FileNotFoundError(f"{INPUT_FILE} not found")

    random.seed(RANDOM_SEED)
    df = load_training_csv(INPUT_FILE)
    if "vehicle_id" not in df.columns:
        raise ValueError("dataML.csv must contain a vehicle_id column")

    df = df.copy()
    df["vehicle_id"] = df["vehicle_id"].astype(str).str.strip()

    if "label" in df.columns:
        df = df[df["label"].astype(str).str.upper() == "NORMAL"]

    # Filter out exact duplicates before generating training sets.
    df = df.drop_duplicates(subset=["vehicle_id", "temperature", "humidity", "accel_magnitude"])

    groups = df.groupby("vehicle_id")

    vehicle_ids = sorted(groups.groups.keys())
    if not vehicle_ids:
        raise ValueError("No vehicle_id values found in dataML.csv")

    header = ("#ifndef TRAINING_DATA_H\n"
              "#define TRAINING_DATA_H\n\n"
              "#include <Arduino.h>\n"
              "#include <array>\n\n"
              "// Auto-generated from dataML.csv\n"
              "// Use generate_training_data_h.py to refresh this file.\n"
              "// Filtered duplicate packets and non-NORMAL rows.\n"
              "// Generated values are constrained to safe military ammunition thresholds for:\n"
              "//   - infantry ammo (10-25°C, 30-65%% RH)\n"
              "//   - explosives/propellant (10-40°C, RH <= 60%%)\n"
              "//   - smart munitions (-20-40°C, 40-50%% RH)\n"
              "// Acceleration magnitudes are also jittered within safe transport ranges.\n"
              "// Only temperature, humidity and accel_magnitude are included in each packet.\n"
              "// Each vehicle array uses its own packet count instead of fixed padding.\n\n"
              "struct TrainingPacket {\n"
              "    float temperature;\n"
              "    float humidity;\n"
              "    float accel_magnitude;\n"
              "};\n\n"
              "constexpr size_t MAX_TRAINING_DATA_COUNT = %d;\n\n") % MAX_TRAINING_COUNT

    body = []
    for vehicle_id in vehicle_ids:
        sanitized = sanitize_identifier(vehicle_id)
        array_name = f"TRAINING_DATA_{sanitized.upper()}"
        group = groups.get_group(vehicle_id).reset_index(drop=True)
        if group.empty:
            print(f"Warning: vehicle_id={vehicle_id} has no valid training packets after filtering")
            continue

        use_replace = len(group) < MAX_TRAINING_COUNT
        sampled = group.sample(n=MAX_TRAINING_COUNT, replace=use_replace, random_state=RANDOM_SEED)
        packets = [jitter_packet(row) for _, row in sampled.iterrows()]
        count = len(packets)
        if use_replace:
            print(f"Info: vehicle_id={vehicle_id} augmented from {len(group)} rows to {count} packets")
        body.append(f"// ========================\n// TRAINING DATA FOR {vehicle_id}\n// Packet count = {count}\n// ========================\n")
        body.append(f"constexpr std::array<TrainingPacket, {count}> {array_name} = {{ {{")
        for packet in packets:
            body.append(format_packet(packet))
        body.append("}} };\n")

    footer = "#endif // TRAINING_DATA_H\n"

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT_FILE.open("w", encoding="utf-8") as f:
        f.write(header)
        f.write("\n".join(body))
        f.write(footer)

    print(f"Wrote {OUTPUT_FILE} with {len(vehicle_ids)} vehicle arrays")


if __name__ == "__main__":
    main()
