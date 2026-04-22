#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import time

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.decomposition import PCA
from sklearn.ensemble import IsolationForest
from sklearn.manifold import TSNE
from sklearn.metrics import (accuracy_score, auc, classification_report,
                             confusion_matrix, precision_recall_fscore_support,
                             roc_auc_score, roc_curve)
from sklearn.model_selection import train_test_split
from sklearn.neighbors import NearestNeighbors
from sklearn.preprocessing import StandardScaler

try:
    import umap.umap_ as umap
except ImportError:
    umap = None

FEATURE_COLUMNS = [
    "temperature",
    "humidity",
    "accel_magnitude",
]

REQUIRED_COLUMNS = FEATURE_COLUMNS + ["label"]
NORMAL_LABELS = ["NORMAL"]
EXTERNAL_ANOMALY_REASON_KEYWORDS = ["rssi", "snr", "gps", "signal_loss", "gps_drift", "longitude", "latitude"]

SAFETY_BASELINE_THRESHOLDS = {
    "general": {"temperature": 50.0, "humidity": 70.0, "accel_magnitude": 3.5},
    "artillery": {"temperature": 45.0, "humidity": 65.0, "accel_magnitude": 3.2},
    "smart": {"temperature": 40.0, "humidity": 60.0, "accel_magnitude": 2.5},
}


def load_dataset(path: Path, drop_external_anomalies: bool = False) -> pd.DataFrame:
    df = pd.read_csv(path)
    missing = [c for c in REQUIRED_COLUMNS if c not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns: {missing}")

    df = df.copy()
    df["label"] = df["label"].astype(str).str.strip().str.upper()
    df["label"] = df["label"].replace({"NORNORAMMAL": "NORMAL"})

    unknown_mask = df["label"] == "UNKNOWN"
    if unknown_mask.any():
        dropped = int(unknown_mask.sum())
        print(f"Dropping {dropped} UNKNOWN rows before training.")
        df = df.loc[~unknown_mask].reset_index(drop=True)

    if drop_external_anomalies and "anomaly_reason" in df.columns:
        reason_mask = df["anomaly_reason"].astype(str).str.lower().str.contains(
            "|".join(EXTERNAL_ANOMALY_REASON_KEYWORDS)
        )
        if reason_mask.any():
            removed = int(reason_mask.sum())
            print(f"Dropping {removed} anomalies caused by RSSI/SNR/GPS from dataset.")
            df = df.loc[~reason_mask].reset_index(drop=True)

    return df


def prepare_data(df: pd.DataFrame, normal_labels=None):
    if normal_labels is None:
        normal_labels = NORMAL_LABELS
    normal_mask = df["label"].isin(normal_labels)
    X = df[FEATURE_COLUMNS].to_numpy(dtype=float)
    y = np.where(normal_mask, 0, 1)  # 0 = normal, 1 = anomaly
    return X, y, normal_mask


def fit_scaler(X_train):
    scaler = StandardScaler()
    scaler.fit(X_train)
    return scaler


def train_knn_outlier(X_train, n_neighbors=5):
    model = NearestNeighbors(n_neighbors=n_neighbors + 1, algorithm="auto")
    model.fit(X_train)
    return model


def knn_anomaly_score(model, X):
    distances, indices = model.kneighbors(X, return_distance=True)
    # drop self-distance when training on normal set
    return distances[:, 1:].mean(axis=1)


def fit_mahalanobis(X_train):
    mean = X_train.mean(axis=0)
    cov = np.cov(X_train, rowvar=False)
    cov += np.eye(cov.shape[0]) * 1e-6
    inv_cov = np.linalg.pinv(cov)
    return mean, inv_cov


def mahalanobis_distance(X, mean, inv_cov):
    delta = X - mean
    left = np.dot(delta, inv_cov)
    d2 = np.sum(left * delta, axis=1)
    return np.sqrt(d2)


def fit_isolation_forest(X_train, contamination=0.01, random_state=42):
    try:
        model = IsolationForest(
            contamination=contamination,
            random_state=random_state,
            n_estimators=100,
        )
    except TypeError:
        model = IsolationForest(
            contamination=contamination,
            random_state=random_state,
            n_estimators=100,
            behaviour="new",
        )
    model.fit(X_train)
    return model


def isolation_scores(model, X):
    # higher score = more normal, so invert for anomaly score
    raw = model.decision_function(X)
    return -raw


def threshold_from_normal(scores, quantile=0.95):
    return np.quantile(scores, quantile)


def rule_based_anomaly_score(X, profile_name="general"):
    thresholds = SAFETY_BASELINE_THRESHOLDS.get(profile_name, SAFETY_BASELINE_THRESHOLDS["general"])
    temp = X[:, 0]
    humidity = X[:, 1]
    accel = X[:, 2]

    temp_excess = np.maximum(0.0, temp - thresholds["temperature"])
    humidity_excess = np.maximum(0.0, humidity - thresholds["humidity"])
    accel_excess = np.maximum(0.0, accel - thresholds["accel_magnitude"])

    return np.maximum(np.maximum(temp_excess, humidity_excess), accel_excess)


def tune_knn(X_train_normal, X_val_scaled, y_val, k_values, quantiles):
    best = None
    best_score = -1.0
    for k in k_values:
        model = train_knn_outlier(X_train_normal, n_neighbors=k)
        train_scores = knn_anomaly_score(model, X_train_normal)
        val_scores = knn_anomaly_score(model, X_val_scaled)
        for q in quantiles:
            threshold = threshold_from_normal(train_scores, quantile=q)
            metrics = evaluate_model(f"kNN k={k} q={q}", val_scores, y_val, threshold)
            if metrics["f1"] > best_score:
                best_score = metrics["f1"]
                best = {"k": k, "quantile": q, "threshold": threshold, "model": model}
    return best


def tune_mahalanobis(X_train_normal, X_val_scaled, y_val, quantiles):
    mean, inv_cov = fit_mahalanobis(X_train_normal)
    val_scores = mahalanobis_distance(X_val_scaled, mean, inv_cov)
    best = None
    best_score = -1.0
    for q in quantiles:
        threshold = threshold_from_normal(mahalanobis_distance(X_train_normal, mean, inv_cov), quantile=q)
        metrics = evaluate_model(f"Mahalanobis q={q}", val_scores, y_val, threshold)
        if metrics["f1"] > best_score:
            best_score = metrics["f1"]
            best = {"quantile": q, "threshold": threshold, "mean": mean, "inv_cov": inv_cov}
    return best


def tune_isolation_forest(X_train_normal, X_val_scaled, y_val, contamination_values):
    best = None
    best_score = -1.0
    for c in contamination_values:
        model = fit_isolation_forest(X_train_normal, contamination=c)
        train_scores = isolation_scores(model, X_train_normal)
        val_scores = isolation_scores(model, X_val_scaled)
        threshold = threshold_from_normal(train_scores, quantile=0.98)
        metrics = evaluate_model(f"IsolationForest c={c}", val_scores, y_val, threshold)
        if metrics["f1"] > best_score:
            best_score = metrics["f1"]
            best = {"contamination": c, "threshold": threshold, "model": model}
    return best

    return np.quantile(scores, quantile)


def evaluate_model(name, scores, y_true, threshold):
    start_time = time.time()
    y_pred = (scores > threshold).astype(int)
    end_time = time.time()
    precision, recall, f1, _ = precision_recall_fscore_support(
        y_true, y_pred, average="binary", zero_division=0
    )
    accuracy = accuracy_score(y_true, y_pred)
    correct = int((y_true == y_pred).sum())
    try:
        roc_auc = roc_auc_score(y_true, scores)
    except ValueError:
        roc_auc = float("nan")
    cm = confusion_matrix(y_true, y_pred)
    runtime_ms = (end_time - start_time) * 1000.0
    return {
        "name": name,
        "threshold": float(threshold),
        "accuracy": float(accuracy),
        "precision": float(precision),
        "recall": float(recall),
        "f1": float(f1),
        "roc_auc": float(roc_auc),
        "correct": correct,
        "total": int(len(y_true)),
        "runtime_ms": float(runtime_ms),
        "confusion_matrix": cm.tolist(),
    }


def plot_score_distributions(score_dict, y_test, thresholds, output_path="comparison_score_distributions.png"):
    plot_data = []
    for name, scores in score_dict.items():
        for score, label in zip(scores, y_test):
            plot_data.append({"model": name, "score": score, "label": "Anomaly" if label == 1 else "Normal"})
    df_plot = pd.DataFrame(plot_data)

    model_colors = {
        "kNN Outlier": "tab:blue",
        "Mahalanobis": "tab:orange",
        "Isolation Forest": "tab:green",
        "MIL-STD Rule": "tab:purple",
    }

    sns.set(style="whitegrid")
    plt.figure(figsize=(12, 8))
    sns.histplot(data=df_plot, x="score", hue="label", multiple="layer", alpha=0.35, kde=True, stat="density")
    for name, thr in thresholds.items():
        color = model_colors.get(name, "black")
        plt.axvline(thr, color=color, linestyle="--", linewidth=2, label=f"{name} threshold")
    plt.title("Score distribution by model and class")
    plt.xlabel("Anomaly score")
    plt.ylabel("Density")
    plt.xlim(left=0)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def plot_model_score_distribution(name: str, scores, y_test, threshold: float, output_path: str):
    df_plot = pd.DataFrame(
        {
            "score": scores,
            "label": ["Anomaly" if label == 1 else "Normal" for label in y_test],
        }
    )
    plt.figure(figsize=(10, 6))
    sns.set(style="whitegrid")
    sns.histplot(
        data=df_plot,
        x="score",
        hue="label",
        multiple="layer",
        alpha=0.35,
        kde=True,
        stat="density",
    )
    plt.axvline(threshold, color="red", linestyle="--", linewidth=2, label=f"{name} threshold")
    plt.title(f"Score distribution: {name}")
    plt.xlabel("Anomaly score")
    plt.ylabel("Density")
    plt.xlim(left=0)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def plot_model_roc_curve(name: str, scores, y_test, output_path: str):
    plt.figure(figsize=(10, 8))
    sns.set(style="whitegrid")
    try:
        fpr, tpr, _ = roc_curve(y_test, scores)
        roc_auc = roc_auc_score(y_test, scores)
        plt.plot(fpr, tpr, label=f"{name} (AUC={roc_auc:.3f})")
        plt.plot([0, 1], [0, 1], color="gray", linestyle="--", linewidth=1)
        plt.title(f"ROC Curve: {name}")
        plt.xlabel("False Positive Rate")
        plt.ylabel("True Positive Rate")
        plt.legend(loc="lower right")
        plt.tight_layout()
        plt.savefig(output_path)
    except ValueError:
        print(f"Cannot plot ROC for {name}: insufficient class variation")
    finally:
        plt.close()


def plot_roc_curves(score_dict, y_test, output_path="comparison_roc_curves.png"):
    plt.figure(figsize=(10, 8))
    sns.set(style="whitegrid")
    for name, scores in score_dict.items():
        try:
            fpr, tpr, _ = roc_curve(y_test, scores)
            roc_auc = roc_auc_score(y_test, scores)
            plt.plot(fpr, tpr, label=f"{name} (AUC={roc_auc:.3f})")
        except ValueError:
            continue
    plt.title("ROC Curves for Anomaly Detectors")
    plt.xlabel("False Positive Rate")
    plt.ylabel("True Positive Rate")
    plt.legend(loc="lower right")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def plot_data_visualization(X, y, output_path="visualize_data.png"):
    pca = PCA(n_components=2)
    components = pca.fit_transform(X)
    df_plot = pd.DataFrame(
        {
            "pc1": components[:, 0],
            "pc2": components[:, 1],
            "label": np.where(y == 1, "ANOMALY", "NORMAL"),
        }
    )

    plt.figure(figsize=(12, 10))
    sns.set(style="whitegrid")
    sns.scatterplot(
        data=df_plot,
        x="pc1",
        y="pc2",
        hue="label",
        palette={"NORMAL": "tab:blue", "ANOMALY": "tab:red"},
        alpha=0.7,
        s=50,
    )
    plt.title("Data visualization: Normal vs Anomaly (PCA 2D projection)")
    plt.xlabel("Principal Component 1")
    plt.ylabel("Principal Component 2")
    plt.legend(title="Label")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def plot_tsne(X, y, output_path="visualize_tsne.png"):
    tsne = TSNE(n_components=2, random_state=42, init="pca", learning_rate="auto", perplexity=30)
    components = tsne.fit_transform(X)
    df_plot = pd.DataFrame(
        {
            "dim1": components[:, 0],
            "dim2": components[:, 1],
            "label": np.where(y == 1, "ANOMALY", "NORMAL"),
        }
    )

    plt.figure(figsize=(12, 10))
    sns.set(style="whitegrid")
    sns.scatterplot(
        data=df_plot,
        x="dim1",
        y="dim2",
        hue="label",
        palette={"NORMAL": "tab:blue", "ANOMALY": "tab:red"},
        alpha=0.7,
        s=50,
    )
    plt.title("Data visualization: Normal vs Anomaly (t-SNE 2D projection)")
    plt.xlabel("t-SNE dimension 1")
    plt.ylabel("t-SNE dimension 2")
    plt.legend(title="Label")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def plot_umap(X, y, output_path="visualize_umap.png"):
    if umap is None:
        print("UMAP is not installed; skipping UMAP visualization.")
        return

    reducer = umap.UMAP(n_components=2, random_state=42, n_neighbors=15, min_dist=0.1)
    components = reducer.fit_transform(X)
    df_plot = pd.DataFrame(
        {
            "dim1": components[:, 0],
            "dim2": components[:, 1],
            "label": np.where(y == 1, "ANOMALY", "NORMAL"),
        }
    )

    plt.figure(figsize=(12, 10))
    sns.set(style="whitegrid")
    sns.scatterplot(
        data=df_plot,
        x="dim1",
        y="dim2",
        hue="label",
        palette={"NORMAL": "tab:blue", "ANOMALY": "tab:red"},
        alpha=0.7,
        s=50,
    )
    plt.title("Data visualization: Normal vs Anomaly (UMAP 2D projection)")
    plt.xlabel("UMAP dimension 1")
    plt.ylabel("UMAP dimension 2")
    plt.legend(title="Label")
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def print_results(results):
    for res in results:
        print("\n" + "=" * 60)
        print(f"Algorithm: {res['name']}")
        print("=" * 60)
        print(f"Threshold: {res['threshold']:.6f}")
        print(f"Accuracy: {res['accuracy']:.4f}")
        print(f"Correct: {res['correct']} / {res['total']}")
        print(f"Precision: {res['precision']:.4f}")
        print(f"Recall: {res['recall']:.4f}")
        print(f"F1-score: {res['f1']:.4f}")
        print(f"ROC AUC: {res['roc_auc']:.4f}")
        print(f"Inference time: {res['runtime_ms']:.2f} ms")
        print("Confusion matrix:")
        print(np.array(res["confusion_matrix"]))


def main():
    parser = argparse.ArgumentParser(description="Compare anomaly detection algorithms")
    parser.add_argument("--dataset", default="dataset.csv", help="CSV dataset path")
    parser.add_argument("--normal-labels", default="NORMAL", help="Labels to treat as normal")
    parser.add_argument("--test-size", type=float, default=0.3, help="Fraction of dataset for test")
    parser.add_argument("--quantile", type=float, default=0.98, help="Threshold quantile on training normal data")
    parser.add_argument("--k", type=int, default=5, help="k for kNN outlier")
    parser.add_argument(
        "--drop-external-anomalies",
        action="store_true",
        help="Remove rows with anomaly_reason related to RSSI/SNR/GPS before training",
    )
    parser.add_argument(
        "--safety-profile",
        choices=["general", "artillery", "smart"],
        default="general",
        help="Safety profile used for the MIL-STD rule-based threshold baseline",
    )
    args = parser.parse_args()

    df = load_dataset(Path(args.dataset), drop_external_anomalies=args.drop_external_anomalies)
    df = df.sample(frac=1, random_state=42).reset_index(drop=True)
    X, y, normal_mask = prepare_data(df, [label.strip().upper() for label in args.normal_labels.split(",")])

    X_temp, X_test, y_temp, y_test = train_test_split(
        X, y, test_size=args.test_size, random_state=42, stratify=y
    )
    X_test_raw = X_test.copy()
    X_train, X_val, y_train, y_val = train_test_split(
        X_temp, y_temp, test_size=0.25, random_state=42, stratify=y_temp
    )

    scaler = fit_scaler(X_train)
    X_train_scaled = scaler.transform(X_train)
    X_val_scaled = scaler.transform(X_val)
    X_test_scaled = scaler.transform(X_test)

    # Train on normal data only for unsupervised detectors
    normal_train_mask = y_train == 0
    X_train_normal = X_train_scaled[normal_train_mask]
    if len(X_train_normal) < args.k + 1:
        raise ValueError("Not enough normal samples for kNN training.")

    # Tune each model on validation set
    knn_grid = tune_knn(X_train_normal, X_val_scaled, y_val, k_values=[3, 5, 7, 9], quantiles=[0.95, 0.98, 0.99])
    man_grid = tune_mahalanobis(X_train_normal, X_val_scaled, y_val, quantiles=[0.95, 0.98, 0.99])
    iforest_grid = tune_isolation_forest(X_train_normal, X_val_scaled, y_val, contamination_values=[0.01, 0.02, 0.05, 0.1])

    print("\nTuning results:")
    print(f"kNN best: k={knn_grid['k']} quantile={knn_grid['quantile']} threshold={knn_grid['threshold']:.6f}")
    print(f"Mahalanobis best: quantile={man_grid['quantile']} threshold={man_grid['threshold']:.6f}")
    print(f"Isolation Forest best: contamination={iforest_grid['contamination']} threshold={iforest_grid['threshold']:.6f}")
    print(f"MIL-STD safety profile: {args.safety_profile}")

    results = []

    # kNN outlier with tuned parameters
    scores_knn = knn_anomaly_score(knn_grid["model"], X_test_scaled)
    results.append(evaluate_model("kNN Outlier", scores_knn, y_test, knn_grid["threshold"]))

    # Mahalanobis with tuned threshold
    scores_mah = mahalanobis_distance(X_test_scaled, man_grid["mean"], man_grid["inv_cov"])
    results.append(evaluate_model("Mahalanobis", scores_mah, y_test, man_grid["threshold"]))

    # Isolation Forest with tuned contamination
    scores_if = isolation_scores(iforest_grid["model"], X_test_scaled)
    results.append(evaluate_model("Isolation Forest", scores_if, y_test, iforest_grid["threshold"]))

    # Rule-based MIL-STD threshold baseline
    scores_rule = rule_based_anomaly_score(X_test_raw, profile_name=args.safety_profile)
    results.append(evaluate_model("MIL-STD Rule", scores_rule, y_test, threshold=0.0))

    print_results(results)

    score_dict = {
        "kNN Outlier": scores_knn,
        "Mahalanobis": scores_mah,
        "Isolation Forest": scores_if,
        "MIL-STD Rule": scores_rule,
    }
    thresholds = {
        "kNN Outlier": knn_grid["threshold"],
        "Mahalanobis": man_grid["threshold"],
        "Isolation Forest": iforest_grid["threshold"],
        "MIL-STD Rule": 0.0,
    }

    plot_score_distributions(
        {k: v for k, v in score_dict.items() if k != "MIL-STD Rule"},
        y_test,
        {k: v for k, v in thresholds.items() if k != "MIL-STD Rule"},
    )
    plot_roc_curves(
        {k: v for k, v in score_dict.items() if k != "MIL-STD Rule"},
        y_test,
    )

    plot_model_score_distribution(
        "kNN Outlier", scores_knn, y_test, knn_grid["threshold"], "score_distribution_knn.png"
    )
    plot_model_score_distribution(
        "Mahalanobis", scores_mah, y_test, man_grid["threshold"], "score_distribution_mahalanobis.png"
    )
    plot_model_score_distribution(
        "Isolation Forest", scores_if, y_test, iforest_grid["threshold"], "score_distribution_isolation_forest.png"
    )

    plot_model_roc_curve("kNN Outlier", scores_knn, y_test, "roc_curve_knn.png")
    plot_model_roc_curve("Mahalanobis", scores_mah, y_test, "roc_curve_mahalanobis.png")
    plot_model_roc_curve("Isolation Forest", scores_if, y_test, "roc_curve_isolation_forest.png")

    plot_data_visualization(X_test_scaled, y_test)
    plot_tsne(X_test_scaled, y_test)
    plot_umap(X_test_scaled, y_test)

    summary = {
        "dataset_samples": int(len(df)),
        "normal_samples": int(np.sum(y == 0)),
        "anomaly_samples": int(np.sum(y == 1)),
        "features": FEATURE_COLUMNS,
        "results": results,
    }
    with open("anomaly_comparison_results.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    print("\nSaved comparison results to anomaly_comparison_results.json")


if __name__ == "__main__":
    main()
