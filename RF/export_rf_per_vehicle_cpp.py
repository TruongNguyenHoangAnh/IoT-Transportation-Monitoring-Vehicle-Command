"""
Export per-vehicle RF anomaly detection kNN training data to C++ headers.
Creates RF_TRAINING_TRANSPORT1 through RF_TRAINING_TRANSPORT5.
"""

import pandas as pd
import json

def export_per_vehicle_headers():
    """Export each vehicle's training data to separate C++ headers."""
    
    # Load downsampled dataset
    df = pd.read_csv("dataset_per_vehicle_optimized.csv")
    
    # Load model info
    with open("rf_knn_model_per_vehicle_info.json", "r") as f:
        model_info = json.load(f)
    
    print("=" * 70)
    print("Exporting Per-Vehicle RF Models to C++")
    print("=" * 70)
    
    vehicles = sorted(df["vehicle_id"].unique())
    
    for vehicle_id in vehicles:
        # Get vehicle data
        vehicle_data = df[df["vehicle_id"] == vehicle_id].copy()
        vehicle_data = vehicle_data.reset_index(drop=True)
        
        # Get model info
        model_metrics = model_info[vehicle_id]
        k_neighbors = model_metrics["k_neighbors"]
        test_accuracy = model_metrics["test_accuracy"]
        
        # Create C++ variable name
        cpp_var_name = f"RF_TRAINING_{vehicle_id.upper().replace('-', '_')}"
        
        # Generate C++ header
        header_content = generate_cpp_header(
            vehicle_id=vehicle_id,
            cpp_var_name=cpp_var_name,
            data=vehicle_data,
            k_neighbors=k_neighbors,
            test_accuracy=test_accuracy
        )
        
        # Save header file
        header_file = f"include/rf_training_{vehicle_id.lower().replace('-', '_')}.h"
        with open(header_file, "w") as f:
            f.write(header_content)
        
        print(f"\n✅ {cpp_var_name}")
        print(f"   File: {header_file}")
        print(f"   Samples: {len(vehicle_data)}")
        print(f"   k: {k_neighbors}")
        print(f"   Test Accuracy: {test_accuracy:.2%}")
    
    print(f"\n{'='*70}")
    print("✨ Export complete!")
    print(f"{'='*70}\n")

def generate_cpp_header(vehicle_id, cpp_var_name, data, k_neighbors, test_accuracy):
    """Generate C++ header with training data."""
    
    # Prepare data
    samples = []
    for idx, row in data.iterrows():
        rssi = int(row["rssi"])
        snr = int(row["snr"] * 10)  # Convert to int (multiply by 10)
        interval = int(row["interval"])
        label = int(row["label"])
        samples.append(f"    {{{rssi}, {snr}, {interval}, {label}}}")
    
    samples_str = ",\n".join(samples)
    
    header_guard = f"RF_TRAINING_{vehicle_id.upper().replace('-', '_')}_H"
    
    header = f"""#pragma once
#ifndef {header_guard}
#define {header_guard}

/**
 * RF Anomaly Detection - Per-Vehicle Training Data
 * 
 * Vehicle: {vehicle_id}
 * Total Samples: {len(data)}
 * k-Nearest Neighbors: {k_neighbors}
 * Test Accuracy: {test_accuracy:.2%}
 * 
 * Feature Definitions:
 * - RSSI: Received Signal Strength Indicator (dBm) [-120, -30]
 * - SNR: Signal-to-Noise Ratio (dB * 10) [-200, 150]
 * - Interval: Packet Interval (ms) [800, 3000]
 * - Label: 0=NORMAL, 1=WARNING, 2=CRITICAL
 * 
 * Class Distribution:
 * - NORMAL:   {len(data[data['label']==0])} samples
 * - WARNING:  {len(data[data['label']==1])} samples
 * - CRITICAL: {len(data[data['label']==2])} samples
 */

// RFSample struct defined in RFKnnPredictorPerVehicle.h
constexpr std::array<RFSample, {len(data)}> {cpp_var_name} = {{{{
{samples_str}
}}}};

#endif  // {header_guard}
"""
    
    return header

if __name__ == "__main__":
    export_per_vehicle_headers()
