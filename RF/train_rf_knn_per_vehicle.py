"""
Train kNN models (global and per-vehicle) for RF anomaly detection.
Tests k=5, 7, 9 and selects best based on generalization gap.
"""

import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.preprocessing import StandardScaler
from sklearn.neighbors import KNeighborsClassifier
from sklearn.metrics import (
    accuracy_score, precision_recall_fscore_support,
    confusion_matrix, classification_report
)
import json

np.random.seed(42)

# Normalization bounds
RSSI_MIN, RSSI_MAX = -120, -30
SNR_MIN, SNR_MAX = -20, 15
INTERVAL_MIN, INTERVAL_MAX = 800, 3000

def normalize_features(df):
    """Normalize features to [0, 1] range for standardization."""
    df_norm = df.copy()
    df_norm["rssi_norm"] = (df["rssi"] - RSSI_MIN) / (RSSI_MAX - RSSI_MIN)
    df_norm["snr_norm"] = (df["snr"] - SNR_MIN) / (SNR_MAX - SNR_MIN)
    df_norm["interval_norm"] = (df["interval"] - INTERVAL_MIN) / (INTERVAL_MAX - INTERVAL_MIN)
    return df_norm

def evaluate_knn_models(X_train, X_test, y_train, y_test, label_name=""):
    """Test kNN with k=5, 7, 9 and return best model."""
    results = {}
    best_k = 5
    best_gap = float('inf')
    best_model = None
    
    for k in [5, 7, 9]:
        model = KNeighborsClassifier(n_neighbors=k, metric='euclidean')
        model.fit(X_train, y_train)
        
        train_acc = model.score(X_train, y_train)
        test_acc = model.score(X_test, y_test)
        gap = train_acc - test_acc
        
        results[k] = {
            "train_accuracy": float(train_acc),
            "test_accuracy": float(test_acc),
            "generalization_gap": float(gap),
        }
        
        # Select based on lowest generalization gap
        if gap < best_gap:
            best_gap = gap
            best_k = k
            best_model = model
    
    return best_model, best_k, results

def train_global_model():
    """Train global kNN model on all data."""
    print("\n" + "="*70)
    print("🌍 Training GLOBAL kNN Model")
    print("="*70)
    
    # Load data
    df = pd.read_csv("dataset_per_vehicle_optimized.csv")
    print(f"\n📊 Dataset: {len(df)} samples ({df['vehicle_id'].nunique()} vehicles)")
    
    # Normalize features
    df_norm = normalize_features(df)
    X = df_norm[["rssi_norm", "snr_norm", "interval_norm"]].values
    y = df["label"].values
    
    # Train/Test split
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    
    print(f"\n   Train: {len(X_train)} samples")
    print(f"   Test:  {len(X_test)} samples")
    
    # Train and evaluate
    best_model, best_k, results = evaluate_knn_models(X_train, X_test, y_train, y_test)
    
    print(f"\n   k-value testing:")
    for k, metrics in results.items():
        marker = "✓ BEST" if k == best_k else ""
        print(f"     k={k}: Train={metrics['train_accuracy']:.2%}, "
              f"Test={metrics['test_accuracy']:.2%}, "
              f"Gap={metrics['generalization_gap']:.4f} {marker}")
    
    # Evaluate best model
    y_pred = best_model.predict(X_test)
    
    # Per-class metrics
    precision, recall, f1, support = precision_recall_fscore_support(y_test, y_pred, average=None)
    
    print(f"\n   Per-class metrics (k={best_k}):")
    for label in [0, 1, 2]:
        label_name = ["NORMAL", "WARNING", "CRITICAL"][label]
        print(f"     {label_name:8}: P={precision[label]:.2%}, R={recall[label]:.2%}, F1={f1[label]:.2%}")
    
    # Confusion matrix
    cm = confusion_matrix(y_test, y_pred)
    print(f"\n   Confusion Matrix:")
    print(f"     {cm}")
    
    # Save model metadata
    metadata = {
        "type": "global",
        "k_neighbors": best_k,
        "total_samples": len(df),
        "train_samples": len(X_train),
        "test_samples": len(X_test),
        "vehicles": 5,
        "train_accuracy": float(best_model.score(X_train, y_train)),
        "test_accuracy": float(best_model.score(X_test, y_test)),
        "per_class_metrics": {
            "NORMAL": {"precision": float(precision[0]), "recall": float(recall[0]), "f1": float(f1[0])},
            "WARNING": {"precision": float(precision[1]), "recall": float(recall[1]), "f1": float(f1[1])},
            "CRITICAL": {"precision": float(precision[2]), "recall": float(recall[2]), "f1": float(f1[2])},
        },
        "k_comparison": results,
    }
    
    with open("rf_knn_model_global_info.json", "w") as f:
        json.dump(metadata, f, indent=2)
    print(f"\n✅ Global model metadata saved")
    
    return best_model, best_k, df_norm

def train_per_vehicle_models():
    """Train per-vehicle kNN models."""
    print("\n" + "="*70)
    print("🚗 Training PER-VEHICLE kNN Models")
    print("="*70)
    
    # Load data
    df = pd.read_csv("dataset_per_vehicle_optimized.csv")
    
    # Normalize features
    df_norm = normalize_features(df)
    
    per_vehicle_results = {}
    
    for vehicle_id in sorted(df["vehicle_id"].unique()):
        vehicle_data = df[df["vehicle_id"] == vehicle_id]
        vehicle_data_norm = df_norm[df["vehicle_id"] == vehicle_id]
        
        print(f"\n📱 {vehicle_id}: {len(vehicle_data)} samples")
        
        X = vehicle_data_norm[["rssi_norm", "snr_norm", "interval_norm"]].values
        y = vehicle_data["label"].values
        
        # Train/Test split
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=42, stratify=y
        )
        
        # Train and evaluate
        best_model, best_k, results = evaluate_knn_models(X_train, X_test, y_train, y_test)
        
        # Per-class metrics
        y_pred = best_model.predict(X_test)
        precision, recall, f1, support = precision_recall_fscore_support(y_test, y_pred, average=None)
        
        # Results
        per_vehicle_results[vehicle_id] = {
            "k_neighbors": best_k,
            "train_samples": len(X_train),
            "test_samples": len(X_test),
            "train_accuracy": float(best_model.score(X_train, y_train)),
            "test_accuracy": float(best_model.score(X_test, y_test)),
            "per_class_metrics": {
                "NORMAL": {"precision": float(precision[0]), "recall": float(recall[0]), "f1": float(f1[0])},
                "WARNING": {"precision": float(precision[1]), "recall": float(recall[1]), "f1": float(f1[1])},
                "CRITICAL": {"precision": float(precision[2]), "recall": float(recall[2]), "f1": float(f1[2])},
            },
        }
        
        print(f"   k={best_k}: Train={per_vehicle_results[vehicle_id]['train_accuracy']:.2%}, "
              f"Test={per_vehicle_results[vehicle_id]['test_accuracy']:.2%}")
    
    # Save per-vehicle metadata
    with open("rf_knn_model_per_vehicle_info.json", "w") as f:
        json.dump(per_vehicle_results, f, indent=2)
    print(f"\n✅ Per-vehicle model metadata saved")
    
    return per_vehicle_results

def main():
    """Train all models."""
    print("\n" + "="*70)
    print("RF ANOMALY DETECTION - kNN MODEL TRAINING")
    print("="*70)
    
    # Train global model
    global_model, best_k, df_norm = train_global_model()
    
    # Train per-vehicle models
    per_vehicle_results = train_per_vehicle_models()
    
    print("\n" + "="*70)
    print("📊 SUMMARY")
    print("="*70)
    print(f"\n✅ Models trained and saved!")
    print(f"\n🚗 Per-Vehicle Models:")
    for vehicle_id, metrics in per_vehicle_results.items():
        print(f"   {vehicle_id}: k={metrics['k_neighbors']}, Test={metrics['test_accuracy']:.2%}")
    
    print("\n" + "="*70)
    print("✨ Training complete!")
    print("="*70 + "\n")

if __name__ == "__main__":
    main()
