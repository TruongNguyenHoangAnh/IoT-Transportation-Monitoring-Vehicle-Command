"""
Downsample per-vehicle RF anomaly dataset using K-means clustering.
Maintains vehicle_id and stratified class distribution.
"""

import numpy as np
import pandas as pd
from sklearn.cluster import KMeans
from sklearn.preprocessing import StandardScaler
import json

np.random.seed(42)

# Normalization bounds
RSSI_MIN, RSSI_MAX = -120, -30
SNR_MIN, SNR_MAX = -20, 15
INTERVAL_MIN, INTERVAL_MAX = 800, 3000

def normalize_features(df):
    """Normalize features to [0, 1] range."""
    df_norm = df.copy()
    df_norm["rssi_norm"] = (df["rssi"] - RSSI_MIN) / (RSSI_MAX - RSSI_MIN)
    df_norm["snr_norm"] = (df["snr"] - SNR_MIN) / (SNR_MAX - SNR_MIN)
    df_norm["interval_norm"] = (df["interval"] - INTERVAL_MIN) / (INTERVAL_MAX - INTERVAL_MIN)
    return df_norm

def downsample_per_vehicle(df, target_samples=600):
    """Downsample data per vehicle using K-means."""
    downsampled = []
    
    for vehicle_id in sorted(df["vehicle_id"].unique()):
        vehicle_data = df[df["vehicle_id"] == vehicle_id].copy()
        print(f"\n📱 {vehicle_id}: {len(vehicle_data)} → {target_samples} samples")
        
        # Downsample while maintaining class distribution
        vehicle_downsampled = downsample_vehicle(vehicle_data, target_samples)
        downsampled.append(vehicle_downsampled)
        
        # Print class distribution for this vehicle
        for label in [0, 1, 2]:
            label_name = ["NORMAL", "WARNING", "CRITICAL"][label]
            count = len(vehicle_downsampled[vehicle_downsampled["label"] == label])
            print(f"  {label_name:8}: {count} samples")
    
    return pd.concat(downsampled, ignore_index=True)

def downsample_vehicle(vehicle_data, target_samples):
    """Downsample a single vehicle's data using K-means stratified by class."""
    downsampled = []
    
    # Get class distribution
    class_counts = vehicle_data["label"].value_counts().to_dict()
    total = len(vehicle_data)
    
    for label in [0, 1, 2]:
        if label not in class_counts:
            continue
        
        # Proportional target samples for this class
        class_ratio = class_counts[label] / total
        class_target = max(1, int(target_samples * class_ratio))
        
        # Get data for this class
        class_data = vehicle_data[vehicle_data["label"] == label].copy()
        
        if len(class_data) <= class_target:
            # Keep all samples if smaller than target
            downsampled.append(class_data)
        else:
            # Apply K-means clustering
            features = class_data[["rssi", "snr", "interval"]].values
            
            # Normalize for K-means
            scaler = StandardScaler()
            features_scaled = scaler.fit_transform(features)
            
            # Fit K-means
            kmeans = KMeans(n_clusters=class_target, random_state=42, n_init=10)
            kmeans.fit(features_scaled)
            
            # Get centroids back to original scale
            centroids = scaler.inverse_transform(kmeans.cluster_centers_)
            
            # Create dataframe of centroids
            centroid_df = pd.DataFrame({
                "vehicle_id": class_data["vehicle_id"].iloc[0],
                "rssi": centroids[:, 0].astype(int),
                "snr": np.round(centroids[:, 1], 1),
                "interval": centroids[:, 2].astype(int),
                "label": label,
            })
            
            downsampled.append(centroid_df)
    
    return pd.concat(downsampled, ignore_index=True)

def main():
    """Downsample per-vehicle RF anomaly dataset."""
    print("=" * 70)
    print("Downsampling Per-Vehicle RF Anomaly Dataset")
    print("=" * 70)
    
    # Load original dataset
    df = pd.read_csv("dataset_per_vehicle.csv")
    print(f"\n📊 Original dataset: {len(df)} samples")
    print(f"   Vehicles: {df['vehicle_id'].nunique()}")
    print(f"   Classes: NORMAL={len(df[df['label']==0])}, WARNING={len(df[df['label']==1])}, CRITICAL={len(df[df['label']==2])}")
    
    # Downsample: 10000 → 3000 (3.3x compression, 600 per vehicle)
    df_downsampled = downsample_per_vehicle(df, target_samples=600)
    
    print(f"\n{'='*70}")
    print(f"📊 Downsampled dataset: {len(df_downsampled)} samples")
    print(f"   Vehicles: {df_downsampled['vehicle_id'].nunique()}")
    
    # Global statistics
    print(f"\n   Global class distribution:")
    for label in [0, 1, 2]:
        label_name = ["NORMAL", "WARNING", "CRITICAL"][label]
        count = len(df_downsampled[df_downsampled["label"] == label])
        pct = (count / len(df_downsampled)) * 100
        print(f"     {label_name:8}: {count:4} samples ({pct:5.1f}%)")
    
    # Compression ratio per vehicle
    print(f"\n   Compression per vehicle:")
    for vehicle_id in sorted(df_downsampled["vehicle_id"].unique()):
        original_count = len(df[df["vehicle_id"] == vehicle_id])
        downsampled_count = len(df_downsampled[df_downsampled["vehicle_id"] == vehicle_id])
        compression = original_count / downsampled_count
        print(f"     Vehicle {vehicle_id}: {original_count} → {downsampled_count} ({compression:.1f}x compression)")
    
    # Feature statistics
    print(f"\n   Feature statistics:")
    print(df_downsampled[["rssi", "snr", "interval"]].describe())
    
    # Save downsampled dataset
    output_file = "dataset_per_vehicle_optimized.csv"
    df_downsampled.to_csv(output_file, index=False)
    print(f"\n✅ Downsampled dataset saved to: {output_file}")
    
    # Save metadata
    metadata = {
        "original_samples": len(df),
        "downsampled_samples": len(df_downsampled),
        "compression_ratio": len(df) / len(df_downsampled),
        "vehicles": int(df_downsampled["vehicle_id"].nunique()),
        "samples_per_vehicle": int(len(df_downsampled) / df_downsampled["vehicle_id"].nunique()),
        "class_distribution": {
            "NORMAL": int(len(df_downsampled[df_downsampled["label"] == 0])),
            "WARNING": int(len(df_downsampled[df_downsampled["label"] == 1])),
            "CRITICAL": int(len(df_downsampled[df_downsampled["label"] == 2])),
        },
    }
    
    metadata_file = "dataset_per_vehicle_optimized_metadata.json"
    with open(metadata_file, "w") as f:
        json.dump(metadata, f, indent=2)
    print(f"✅ Metadata saved to: {metadata_file}")
    
    print(f"\n{'='*70}")
    print("✨ Downsampling complete!")
    print(f"{'='*70}\n")

if __name__ == "__main__":
    main()
