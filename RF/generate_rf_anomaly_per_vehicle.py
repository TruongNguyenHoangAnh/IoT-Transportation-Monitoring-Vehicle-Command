"""
Generate RF anomaly detection dataset with per-vehicle characteristics.
Each vehicle has different RSSI ranges based on antenna position and environment.

Vehicle Profiles:
- Transport-1: RSSI ≈ -60~-70 dBm (good reception)
- Transport-2: RSSI ≈ -80~-95 dBm (medium reception)
- Transport-3: RSSI ≈ -70~-85 dBm (mixed reception)
- Transport-4: RSSI ≈ -75~-90 dBm (variable reception)
- Transport-5: RSSI ≈ -65~-80 dBm (good reception with noise)
"""

import numpy as np
import pandas as pd
from scipy import stats
import json

# Set random seed for reproducibility
np.random.seed(42)

# Vehicle RF characteristics
VEHICLE_PROFILES = {
    1: {
        "name": "Transport-1",
        "rssi_normal_mean": -65,
        "rssi_normal_std": 4,
        "rssi_warning_mean": -80,
        "rssi_warning_std": 6,
        "rssi_critical_mean": -98,
        "rssi_critical_std": 4,
    },
    2: {
        "name": "Transport-2",
        "rssi_normal_mean": -85,
        "rssi_normal_std": 5,
        "rssi_warning_mean": -90,
        "rssi_warning_std": 7,
        "rssi_critical_mean": -105,
        "rssi_critical_std": 4,
    },
    3: {
        "name": "Transport-3",
        "rssi_normal_mean": -75,
        "rssi_normal_std": 4.5,
        "rssi_warning_mean": -85,
        "rssi_warning_std": 6.5,
        "rssi_critical_mean": -102,
        "rssi_critical_std": 4,
    },
    4: {
        "name": "Transport-4",
        "rssi_normal_mean": -80,
        "rssi_normal_std": 5,
        "rssi_warning_mean": -87,
        "rssi_warning_std": 7,
        "rssi_critical_mean": -103,
        "rssi_critical_std": 4,
    },
    5: {
        "name": "Transport-5",
        "rssi_normal_mean": -70,
        "rssi_normal_std": 4,
        "rssi_warning_mean": -82,
        "rssi_warning_std": 6,
        "rssi_critical_mean": -100,
        "rssi_critical_std": 4,
    },
}

# Common SNR and Interval characteristics (same for all vehicles)
SNR_NORMAL_MEAN = 8.5
SNR_NORMAL_STD = 2.0
SNR_WARNING_MEAN = 2.5
SNR_WARNING_STD = 2.5
SNR_CRITICAL_MEAN = -4.5
SNR_CRITICAL_STD = 3.0

INTERVAL_NORMAL_MEAN = 1000
INTERVAL_NORMAL_STD = 150
INTERVAL_WARNING_MEAN = 1900
INTERVAL_WARNING_STD = 300
INTERVAL_CRITICAL_MEAN = 2700
INTERVAL_CRITICAL_STD = 200

# Normalization bounds
RSSI_MIN, RSSI_MAX = -120, -30
SNR_MIN, SNR_MAX = -20, 15
INTERVAL_MIN, INTERVAL_MAX = 800, 3000

def clip_value(value, min_val, max_val):
    """Clip value to range."""
    return np.clip(value, min_val, max_val)

def generate_vehicle_dataset(vehicle_id, num_samples=2800):
    """Generate dataset for a specific vehicle."""
    profile = VEHICLE_PROFILES[vehicle_id]
    vehicle_name = profile["name"]
    
    # Samples per class (700 NORMAL, 420 WARNING, 280 CRITICAL per vehicle)
    n_normal = int(num_samples * 0.25)
    n_warning = int(num_samples * 0.15)
    n_critical = int(num_samples * 0.10)
    
    samples = []
    
    # NORMAL class (label 0)
    for _ in range(n_normal):
        rssi = clip_value(np.random.normal(profile["rssi_normal_mean"], profile["rssi_normal_std"]), RSSI_MIN, RSSI_MAX)
        snr = clip_value(np.random.normal(SNR_NORMAL_MEAN, SNR_NORMAL_STD), SNR_MIN, SNR_MAX)
        interval = clip_value(np.random.normal(INTERVAL_NORMAL_MEAN, INTERVAL_NORMAL_STD), INTERVAL_MIN, INTERVAL_MAX)
        samples.append({
            "vehicle_id": vehicle_name,
            "rssi": int(rssi),
            "snr": int(snr * 10) / 10,  # One decimal place
            "interval": int(interval),
            "label": 0,
        })
    
    # WARNING class (label 1) - overlaps with both NORMAL and CRITICAL
    for _ in range(n_warning):
        rssi = clip_value(np.random.normal(profile["rssi_warning_mean"], profile["rssi_warning_std"]), RSSI_MIN, RSSI_MAX)
        snr = clip_value(np.random.normal(SNR_WARNING_MEAN, SNR_WARNING_STD), SNR_MIN, SNR_MAX)
        interval = clip_value(np.random.normal(INTERVAL_WARNING_MEAN, INTERVAL_WARNING_STD), INTERVAL_MIN, INTERVAL_MAX)
        samples.append({
            "vehicle_id": vehicle_name,
            "rssi": int(rssi),
            "snr": int(snr * 10) / 10,
            "interval": int(interval),
            "label": 1,
        })
    
    # CRITICAL class (label 2)
    for _ in range(n_critical):
        rssi = clip_value(np.random.normal(profile["rssi_critical_mean"], profile["rssi_critical_std"]), RSSI_MIN, RSSI_MAX)
        snr = clip_value(np.random.normal(SNR_CRITICAL_MEAN, SNR_CRITICAL_STD), SNR_MIN, SNR_MAX)
        interval = clip_value(np.random.normal(INTERVAL_CRITICAL_MEAN, INTERVAL_CRITICAL_STD), INTERVAL_MIN, INTERVAL_MAX)
        samples.append({
            "vehicle_id": vehicle_name,
            "rssi": int(rssi),
            "snr": int(snr * 10) / 10,
            "interval": int(interval),
            "label": 2,
        })
    
    return pd.DataFrame(samples)

def main():
    """Generate per-vehicle RF anomaly datasets."""
    print("=" * 70)
    print("Generating RF Anomaly Detection Dataset (Per-Vehicle)")
    print("=" * 70)
    
    all_dfs = []
    
    for vehicle_id in range(1, 6):
        profile = VEHICLE_PROFILES[vehicle_id]
        print(f"\n📱 {profile['name']}:")
        print(f"   RSSI ranges:")
        print(f"     - NORMAL:   μ={profile['rssi_normal_mean']} dBm, σ={profile['rssi_normal_std']}")
        print(f"     - WARNING:  μ={profile['rssi_warning_mean']} dBm, σ={profile['rssi_warning_std']}")
        print(f"     - CRITICAL: μ={profile['rssi_critical_mean']} dBm, σ={profile['rssi_critical_std']}")
        
        df = generate_vehicle_dataset(vehicle_id, num_samples=4000)
        all_dfs.append(df)
        
        # Statistics for this vehicle
        for label in [0, 1, 2]:
            label_name = ["NORMAL", "WARNING", "CRITICAL"][label]
            label_data = df[df["label"] == label]
            print(f"   {label_name:8} (n={len(label_data):4}): "
                  f"RSSI μ={label_data['rssi'].mean():6.1f} σ={label_data['rssi'].std():4.1f}, "
                  f"SNR μ={label_data['snr'].mean():5.1f}, "
                  f"Interval μ={label_data['interval'].mean():7.1f}")
    
    # Combine all vehicle datasets
    df_combined = pd.concat(all_dfs, ignore_index=True)
    
    print(f"\n{'=' * 70}")
    print(f"📊 Combined Dataset Statistics:")
    print(f"{'=' * 70}")
    print(f"Total samples: {len(df_combined):,}")
    print(f"Vehicles: 5")
    print(f"Samples per vehicle: {len(df_combined) // 5}")
    print(f"\nGlobal class distribution:")
    for label in [0, 1, 2]:
        label_name = ["NORMAL", "WARNING", "CRITICAL"][label]
        count = len(df_combined[df_combined["label"] == label])
        pct = (count / len(df_combined)) * 100
        print(f"  {label_name:8}: {count:5} samples ({pct:5.1f}%)")
    
    print(f"\nFeature statistics (across all vehicles):")
    print(df_combined[["rssi", "snr", "interval"]].describe())
    
    # Save to CSV
    output_file = "dataset_per_vehicle.csv"
    df_combined.to_csv(output_file, index=False)
    print(f"\n✅ Dataset saved to: {output_file}")
    
    # Save metadata
    metadata = {
        "total_samples": len(df_combined),
        "vehicles": 5,
        "samples_per_vehicle": len(df_combined) // 5,
        "class_distribution": {
            "NORMAL": int(len(df_combined[df_combined["label"] == 0])),
            "WARNING": int(len(df_combined[df_combined["label"] == 1])),
            "CRITICAL": int(len(df_combined[df_combined["label"] == 2])),
        },
        "feature_ranges": {
            "rssi": {"min": int(df_combined["rssi"].min()), "max": int(df_combined["rssi"].max())},
            "snr": {"min": float(df_combined["snr"].min()), "max": float(df_combined["snr"].max())},
            "interval": {"min": int(df_combined["interval"].min()), "max": int(df_combined["interval"].max())},
        },
        "vehicle_profiles": {str(k): v for k, v in VEHICLE_PROFILES.items()},
    }
    
    metadata_file = "dataset_per_vehicle_metadata.json"
    with open(metadata_file, "w") as f:
        json.dump(metadata, f, indent=2)
    print(f"✅ Metadata saved to: {metadata_file}")
    
    print(f"\n{'=' * 70}")
    print("✨ Dataset generation complete!")
    print(f"{'=' * 70}\n")

if __name__ == "__main__":
    main()
