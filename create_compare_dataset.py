#!/usr/bin/env python3
import argparse
from pathlib import Path
import numpy as np
import pandas as pd

FEATURE_COLUMNS = [
    "temperature",
    "humidity",
    "accel_magnitude",
]

MQTT_CSV_HEADER = [
    "received_at",
    "topic",
    "vehicle_id",
    "msg_type",
    "payload_timestamp_ms",
    "payload_seq",
    "transport_id",
    "temperature",
    "humidity",
    "a_g",
    "light_level",
    "tamper",
    "gateway_rssi",
    "gateway_snr",
    "ml_prediction",
    "ml_score",
    "ml_threshold",
    "anomaly_type",
    "severity",
    "detected",
    "detected_value",
    "latitude",
    "longitude",
    "raw_payload",
]

TRAINING_COLUMNS = [
    "vehicle_id",
    "temperature",
    "humidity",
    "accel_magnitude",
    "label",
    "anomaly_type",
    "anomaly_reason",
]

ANOMALY_TYPES = [
    "shock",
    "temperature_extreme",
    "humidity_extreme",
]

ANOMALY_DESCRIPTIONS = {
    "signal_loss_complete": "Mất sóng hoàn toàn - SNR gần 0, RSSI cực thấp",
    "signal_loss": "Mất sóng / rất yếu",
    "weak_signal": "Tín hiệu kém",
    "gps_drift": "GPS lệch",
    "shock": "Rung xóc bất thường",
    "temperature_extreme": "Nhiệt độ bất thường",
    "humidity_extreme": "Độ ẩm bất thường",
}

SAFETY_PROFILES = {
    "general": {
        "temperature": (10.0, 25.0),
        "humidity": (30.0, 65.0),
        "accel": (0.0, 2.5),
    },
    "artillery": {
        "temperature": (10.0, 40.0),
        "humidity": (30.0, 60.0),
        "accel": (0.0, 3.0),
    },
    "smart": {
        "temperature": (-20.0, 40.0),
        "humidity": (40.0, 50.0),
        "accel": (0.0, 2.0),
    },
}

APP_WARNING_THRESHOLDS = {
    "general": {"temperature": 50.0, "humidity": 70.0},
    "artillery": {"temperature": 45.0, "humidity": 65.0},
    "smart": {"temperature": 40.0, "humidity": 60.0},
}


def detect_input_columns(df: pd.DataFrame) -> dict:
    lower_to_original = {c.lower(): c for c in df.columns}

    def find_column(names):
        return next((lower_to_original[name] for name in names if name in lower_to_original), None)

    return {
        "vehicle_id": find_column({"vehicle_id", "transport_id", "device_id"}),
        "temperature": find_column({"temperature", "temp", "t"}),
        "humidity": find_column({"humidity", "h"}),
        "accel_magnitude": find_column({"accel_magnitude", "a_g", "mean_delta"}),
        "label": find_column({"label", "ml_prediction", "prediction"}),
        "anomaly_type": find_column({"anomaly_type"}),
        "anomaly_reason": find_column({"anomaly_reason"}),
        "msg_type": find_column({"msg_type", "message_type", "type"}),
    }


def build_base_dataset(df: pd.DataFrame, input_cols: dict) -> pd.DataFrame:
    result = pd.DataFrame()


    if input_cols["temperature"] is not None:
        result["temperature"] = df[input_cols["temperature"]].astype(float)
    else:
        result["temperature"] = np.random.normal(loc=27.0, scale=1.5, size=len(df))

    if input_cols["humidity"] is not None:
        result["humidity"] = df[input_cols["humidity"]].astype(float)
    else:
        result["humidity"] = np.random.normal(loc=55.0, scale=5.0, size=len(df))

    if input_cols["accel_magnitude"] is not None:
        result["accel_magnitude"] = df[input_cols["accel_magnitude"]].astype(float)
        if input_cols["accel_magnitude"] == "mean_delta":
            result["accel_magnitude"] = (result["accel_magnitude"] / result["accel_magnitude"].max()).fillna(0.0) * 3.0
    else:
        result["accel_magnitude"] = np.random.normal(loc=1.0, scale=0.2, size=len(df))


    if input_cols.get("vehicle_id") is not None:
        result["vehicle_id"] = df[input_cols["vehicle_id"]].astype(str)
    else:
        result["vehicle_id"] = "unknown"

    if input_cols["label"] is not None:
        result["label"] = df[input_cols["label"]].astype(str).str.strip().str.upper()
    else:
        result["label"] = "NORMAL"

    if input_cols.get("anomaly_type") is not None:
        result["anomaly_type"] = df[input_cols["anomaly_type"]].fillna("").astype(str)
    else:
        result["anomaly_type"] = ""

    if input_cols.get("anomaly_reason") is not None:
        result["anomaly_reason"] = df[input_cols["anomaly_reason"]].fillna("").astype(str)
    else:
        result["anomaly_reason"] = ""

    valid_rows = result[FEATURE_COLUMNS].notna().all(axis=1)
    if not valid_rows.all():
        invalid_count = int((~valid_rows).sum())
        print(f"Removed {invalid_count} rows with missing feature data.")
        result = result[valid_rows].copy()

    return result


def create_synthetic_anomaly(sample: pd.Series, anomaly_type: str) -> pd.Series:
    row = sample.to_dict()
    if anomaly_type == "shock":
        row["accel_magnitude"] = max(float(row["accel_magnitude"]), 0.1) * np.random.uniform(2.5, 5.0)
        row["temperature"] = float(row["temperature"]) + np.random.uniform(-3.0, 3.0)
        row["humidity"] = float(row["humidity"]) + np.random.uniform(-3.0, 3.0)
    elif anomaly_type == "temperature_extreme":
        if np.random.rand() < 0.5:
            row["temperature"] = float(row["temperature"]) + np.random.uniform(10.0, 20.0)
        else:
            row["temperature"] = float(row["temperature"]) - np.random.uniform(10.0, 18.0)
        row["humidity"] = float(row["humidity"]) + np.random.uniform(-5.0, 5.0)
    elif anomaly_type == "humidity_extreme":
        if np.random.rand() < 0.5:
            row["humidity"] = float(row["humidity"]) + np.random.uniform(18.0, 30.0)
        else:
            row["humidity"] = float(row["humidity"]) - np.random.uniform(18.0, 30.0)
        row["temperature"] = float(row["temperature"]) + np.random.uniform(-2.0, 2.0)
    row["label"] = "ANOMALY"
    row["anomaly_type"] = anomaly_type
    row["anomaly_reason"] = ANOMALY_DESCRIPTIONS.get(anomaly_type, anomaly_type)
    return pd.Series(row)


def normalize_input_headers(df: pd.DataFrame) -> pd.DataFrame:
    expected = [
        "timestamp_ms",
        "vehicle_id",
        "temperature",
        "humidity",
        "accel_magnitude",
        "label",
        "anomaly_type",
        "anomaly_reason",
        "raw_payload",
    ]

    canonical_map = {}
    lower_to_original = {c.lower(): c for c in df.columns}
    aliases = {
        "a_g": "accel_magnitude",
        "ml_prediction": "label",
        "msg_type": "msg_type",
        "message_type": "msg_type",
        "anomaly_type": "anomaly_type",
        "anomaly_reason": "anomaly_reason",
    }

    for alias, canonical in aliases.items():
        if alias in lower_to_original:
            canonical_map[lower_to_original[alias]] = canonical

    if canonical_map:
        df = df.rename(columns=canonical_map)

    if len(df.columns) == len(MQTT_CSV_HEADER):
        df.columns = MQTT_CSV_HEADER
        return df

    if len(df.columns) == len(expected):
        df.columns = expected
    return df


def filter_telemetry_rows(df: pd.DataFrame, input_cols: dict) -> pd.DataFrame:
    if input_cols.get("msg_type") is None:
        return df

    msg_type = input_cols["msg_type"]
    if msg_type not in df.columns:
        return df

    telemetry_df = df[df[msg_type].astype(str).str.lower() == "telemetry"].copy()
    if len(telemetry_df) != len(df):
        dropped = len(df) - len(telemetry_df)
        print(f"Removed {dropped} non-telemetry input rows.")
    return telemetry_df


def format_reason(reason: str, value: float, threshold: float) -> str:
    return f"{reason}: giá trị {value:.1f} vượt ngưỡng {threshold:.1f}"


def generate_safe_row(profile_name: str, vehicle_id: str = "Transport-1") -> dict:
    profile = SAFETY_PROFILES[profile_name]
    return {
        "vehicle_id": vehicle_id,
        "temperature": round(float(np.random.uniform(*profile["temperature"])), 2),
        "humidity": round(float(np.random.uniform(*profile["humidity"])), 2),
        "accel_magnitude": round(float(np.random.uniform(*profile["accel"])), 3),
        "label": "NORMAL",
        "anomaly_type": "",
        "anomaly_reason": "",
    }


def generate_threshold_anomaly(profile_name: str, anomaly_type: str, vehicle_id: str = "Transport-1") -> dict:
    profile = SAFETY_PROFILES[profile_name]
    row = generate_safe_row(profile_name, vehicle_id)
    warning = APP_WARNING_THRESHOLDS[profile_name]
    if anomaly_type == "temperature_extreme":
        row["temperature"] = round(float(np.random.uniform(warning["temperature"] + 1.0, warning["temperature"] + 15.0)), 2)
        row["anomaly_type"] = "temperature_extreme"
        row["anomaly_reason"] = format_reason("Nhiệt độ vượt ngưỡng an toàn", row["temperature"], warning["temperature"])
    elif anomaly_type == "humidity_extreme":
        row["humidity"] = round(float(np.random.uniform(warning["humidity"] + 1.0, warning["humidity"] + 20.0)), 2)
        row["anomaly_type"] = "humidity_extreme"
        row["anomaly_reason"] = format_reason("Độ ẩm vượt ngưỡng an toàn", row["humidity"], warning["humidity"])
    elif anomaly_type == "shock":
        row["accel_magnitude"] = round(float(np.random.uniform(profile["accel"][1] + 1.0, profile["accel"][1] + 5.0)), 3)
        row["anomaly_type"] = "shock"
        row["anomaly_reason"] = f"Rung xóc quá mức an toàn: {row['accel_magnitude']:.2f} g"
    row["label"] = "ANOMALY"
    return row


def generate_safe_dataset(num_normal: int, num_anomaly: int, seed: int) -> pd.DataFrame:
    np.random.seed(seed)
    rows = []
    profile_names = list(SAFETY_PROFILES.keys())
    for idx in range(num_normal):
        profile_name = np.random.choice(profile_names, p=[0.65, 0.2, 0.15])
        rows.append(generate_safe_row(profile_name, vehicle_id=f"Transport-{(idx % 2) + 1}"))

    for idx in range(num_anomaly):
        profile_name = np.random.choice(profile_names, p=[0.65, 0.2, 0.15])
        anomaly_type = np.random.choice(["temperature_extreme", "humidity_extreme", "shock"], p=[0.45, 0.35, 0.2])
        rows.append(generate_threshold_anomaly(profile_name, anomaly_type, vehicle_id=f"Transport-{(idx % 2) + 1}"))

    return pd.DataFrame(rows, columns=TRAINING_COLUMNS)


def generate_compare_dataset(df: pd.DataFrame, anomaly_rate: float, seed: int) -> pd.DataFrame:
    np.random.seed(seed)
    labels = df["label"].astype(str).str.strip().str.upper()
    if "ANOMALY" in labels.values:
        print("Input already contains anomaly labels. Preserving existing labels and adding synthetic anomalies if needed.")

    normal_df = df[labels == "NORMAL"].copy()
    anomaly_df = df[labels != "NORMAL"].copy()

    if anomaly_rate <= 0 or len(normal_df) == 0:
        return df[TRAINING_COLUMNS].copy()

    num_synth = max(1, int(len(normal_df) * anomaly_rate))
    chosen = normal_df.sample(n=num_synth, replace=False, random_state=seed)
    synthetic_rows = []
    for _, sample in chosen.iterrows():
        anomaly_type = np.random.choice(ANOMALY_TYPES)
        synthetic_rows.append(create_synthetic_anomaly(sample, anomaly_type))

    synthetic_df = pd.DataFrame(synthetic_rows)
    result = pd.concat([df, synthetic_df], ignore_index=True, sort=False)
    result = result.reset_index(drop=True)
    return result[TRAINING_COLUMNS].copy()


def main():
    parser = argparse.ArgumentParser(description="Tạo compare_dataset.csv with synthetic anomalies")
    parser.add_argument("--input", default="dataset.csv", help="Input CSV path (dataset.csv or mqtt_payload_log.csv)")
    parser.add_argument("--output", default="compare_dataset.csv", help="Output CSV path")
    parser.add_argument("--anomaly-rate", type=float, default=0.2, help="Tỷ lệ anomaly được tạo thêm từ normal (0-1)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for anomaly synthesis")
    parser.add_argument("--generate-safe-data", action="store_true", help="Generate a fresh safe dataset for normal rows and threshold-based anomalies")
    parser.add_argument("--num-normal", type=int, default=1000, help="Number of safe normal rows to generate when using --generate-safe-data")
    parser.add_argument("--num-anomaly", type=int, default=200, help="Number of threshold-based anomaly rows to generate when using --generate-safe-data")
    args = parser.parse_args()

    if args.generate_safe_data:
        compare_df = generate_safe_dataset(num_normal=args.num_normal, num_anomaly=args.num_anomaly, seed=args.seed)
    else:
        input_path = Path(args.input)
        if not input_path.exists():
            raise FileNotFoundError(f"Không tìm thấy input file: {input_path}")

        df = pd.read_csv(input_path)
        df = normalize_input_headers(df)
        input_cols = detect_input_columns(df)
        df = filter_telemetry_rows(df, input_cols)
        df_base = build_base_dataset(df, input_cols)
        compare_df = generate_compare_dataset(df_base, anomaly_rate=args.anomaly_rate, seed=args.seed)

    compare_df.to_csv(args.output, index=False)
    print(f"Created {args.output} with {len(compare_df)} rows, including {int((compare_df['label'] == 'ANOMALY').sum())} anomalies.")


if __name__ == "__main__":
    main()

#python create_compare_dataset.py --input compare_dataset.csv --output compare_dataset.csv --anomaly-rate 0.2
