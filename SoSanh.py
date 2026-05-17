import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import time

from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import (
    accuracy_score, recall_score, f1_score,
    roc_auc_score, roc_curve,
    precision_score, confusion_matrix, classification_report
)
from sklearn.linear_model import LogisticRegression

import tensorflow as tf
from tensorflow.keras import layers, models

# ==================== 1. LOAD DATA ====================
DATASET_PATH = "iot_lab_dataset.csv"

df = pd.read_csv(DATASET_PATH)
df.columns = ["t", "h", "a", "anomaly_label"]

df["anomaly_label"] = df["anomaly_label"].map({"NORMAL": 0, "ANOMALY": 1})

X = df[["t", "h", "a"]].values
y = df["anomaly_label"].values

# ==================== 2. ADD NOISE (🔥 giữ từ code cũ) ====================
noise = np.random.normal(0, 0.02, X.shape)
X = X + noise

# ==================== 3. TRAIN-TEST SPLIT ====================
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

# ==================== 4. SCALING ====================
scaler = StandardScaler()
X_train_scaled = scaler.fit_transform(X_train)
X_test_scaled = scaler.transform(X_test)

# ==================== 5. MODEL A: LOGISTIC REGRESSION ====================
lr_model = LogisticRegression(max_iter=500)
lr_model.fit(X_train_scaled, y_train)

# Inference time (per sample - Edge style)
start = time.perf_counter()
for x in X_test_scaled:
    lr_model.predict([x])
end = time.perf_counter()
lr_infer = (end - start) / len(X_test_scaled) * 1000

# Predict
y_pred_lr = lr_model.predict(X_test_scaled)
y_prob_lr = lr_model.predict_proba(X_test_scaled)[:, 1]

# Metrics
lr_acc = accuracy_score(y_test, y_pred_lr)
lr_recall = recall_score(y_test, y_pred_lr)
lr_f1 = f1_score(y_test, y_pred_lr)
lr_auc = roc_auc_score(y_test, y_prob_lr)
lr_precision = precision_score(y_test, y_pred_lr)
lr_cm = confusion_matrix(y_test, y_pred_lr)

# ==================== 6. MODEL B: NEURAL NETWORK ====================
nn_model = models.Sequential([
    layers.Input(shape=(3,)),
    layers.Dense(32, activation='relu'),
    layers.Dropout(0.1),
    layers.Dense(16, activation='relu'),
    layers.Dense(8, activation='relu'),
    layers.Dense(1, activation='sigmoid')
])

nn_model.compile(
    optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
    loss='binary_crossentropy',
    metrics=['accuracy']
)

# Early stopping
early_stop = tf.keras.callbacks.EarlyStopping(
    monitor='val_loss',
    patience=5,
    restore_best_weights=True
)

# Train (🔥 có class_weight từ code cũ)
nn_model.fit(
    X_train_scaled, y_train,
    epochs=50,
    batch_size=32,
    validation_split=0.1,
    class_weight={0:1.0, 1:1.5},
    callbacks=[early_stop],
    verbose=0
)

# Inference time (per sample)
start = time.perf_counter()
nn_model.predict(X_test_scaled, verbose=0)
end = time.perf_counter()

nn_infer = (end - start) / len(X_test_scaled) * 1000

# Predict
y_prob_nn = nn_model.predict(X_test_scaled, verbose=0).flatten()
y_pred_nn = (y_prob_nn > 0.5).astype(int)

# Metrics
nn_acc = accuracy_score(y_test, y_pred_nn)
nn_recall = recall_score(y_test, y_pred_nn)
nn_f1 = f1_score(y_test, y_pred_nn)
nn_auc = roc_auc_score(y_test, y_prob_nn)
nn_precision = precision_score(y_test, y_pred_nn)
nn_cm = confusion_matrix(y_test, y_pred_nn)

# ==================== 7. PRINT TABLE ====================
print("\n===== MODEL COMPARISON (Classification + Edge ML) =====")
print("{:<20} {:<8} {:<8} {:<8} {:<8} {:<10} {:<10}".format(
    "Model", "Acc", "Recall", "F1", "AUC", "Precision", "Infer(ms)"
))
print("-" * 80)

print("{:<20} {:<8.4f} {:<8.4f} {:<8.4f} {:<8.4f} {:<10.4f} {:<10.4f}".format(
    "Logistic Regression",
    lr_acc, lr_recall, lr_f1, lr_auc, lr_precision, lr_infer
))

print("{:<20} {:<8.4f} {:<8.4f} {:<8.4f} {:<8.4f} {:<10.4f} {:<10.4f}".format(
    "Neural Network",
    nn_acc, nn_recall, nn_f1, nn_auc, nn_precision, nn_infer
))

# ==================== 8. CONFUSION MATRIX ====================
print("\n===== CONFUSION MATRIX =====")

print("\n[Logistic Regression]")
print(lr_cm)

print("\n[Neural Network]")
print(nn_cm)

# ==================== 9. CLASSIFICATION REPORT ====================
print("\n===== CLASSIFICATION REPORT (NN) =====")
print(classification_report(y_test, y_pred_nn))

# ==================== 10. ROC CURVE ====================
fpr_lr, tpr_lr, _ = roc_curve(y_test, y_prob_lr)
fpr_nn, tpr_nn, _ = roc_curve(y_test, y_prob_nn)

plt.figure()
plt.plot(fpr_lr, tpr_lr, label=f"LR (AUC={lr_auc:.3f})")
plt.plot(fpr_nn, tpr_nn, label=f"NN (AUC={nn_auc:.3f})")
plt.plot([0,1],[0,1],'--')

plt.xlabel("False Positive Rate")
plt.ylabel("True Positive Rate")
plt.title("ROC Curve Comparison")
plt.legend()

plt.savefig("roc_comparison.png")
plt.close()

print("\nROC curve saved to: roc_comparison.png")

# ==================== 11. SCALER EXPORT ====================
print("\n=== SCALER PARAMETERS ===")
print("mean:", scaler.mean_)
print("std :", scaler.scale_)

# ==================== 12. SANITY CHECK ====================
print("\n=== SANITY CHECK ===")

tests = [
    ("NORMAL", [30, 60, 0.1]),
    ("HIGH TEMP", [40, 60, 0.1]),
    ("LOW HUMI", [30, 10, 0.1]),
    ("ACCEL SPIKE", [30, 60, 3.0]),
]

for name, sample in tests:
    pred = nn_model.predict(scaler.transform([sample]), verbose=0)[0][0]
    print(f"{name}: {pred:.4f}")
    
# ==================== 13. SAVE MODEL ====================
nn_model.save("model_nn.keras")

# ==================== 14. TFLITE EXPORT ====================
converter = tf.lite.TFLiteConverter.from_keras_model(nn_model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]

tflite_model = converter.convert()

with open("model_nn.tflite", "wb") as f:
    f.write(tflite_model)

print("\nDONE -> model_nn.tflite")