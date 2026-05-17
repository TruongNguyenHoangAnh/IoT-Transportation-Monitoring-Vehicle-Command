import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix
import tensorflow as tf

# =========================
# LOAD DATA
# =========================
df = pd.read_csv("iot_lab_dataset.csv")

# =========================
# FEATURES & LABEL
# =========================
X = df[["t", "h", "a"]].values
y = df["anomaly_label"].map({"NORMAL":0, "ANOMALY":1}).values

# =========================
# ADD NOISE (🔥 CRITICAL)
# giúp model không học cứng rule
# =========================
noise = np.random.normal(0, 0.02, X.shape)
X = X + noise

# =========================
# SPLIT
# =========================
X_train, X_test, y_train, y_test = train_test_split(
    X, y,
    test_size=0.2,
    random_state=42,
    stratify=y
)

# =========================
# NORMALIZATION
# =========================
scaler = StandardScaler()
X_train = scaler.fit_transform(X_train)
X_test = scaler.transform(X_test)

# =========================
# MODEL (TinyML nhưng thông minh hơn)
# =========================
model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(3,)),

    tf.keras.layers.Dense(32, activation='relu'),
    tf.keras.layers.Dropout(0.1),   # 🔥 chống overfit

    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(8, activation='relu'),

    tf.keras.layers.Dense(1, activation='sigmoid')
])

model.compile(
    optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
    loss='binary_crossentropy',
    metrics=['accuracy']
)

# =========================
# CALLBACKS (🔥 QUAN TRỌNG)
# =========================
callbacks = [
    tf.keras.callbacks.EarlyStopping(
        monitor='val_loss',
        patience=5,
        restore_best_weights=True
    )
]

# =========================
# TRAIN
# =========================
history = model.fit(
    X_train, y_train,
    epochs=50,
    batch_size=32,
    validation_split=0.2,
    class_weight={0:1.0, 1:1.5},
    callbacks=callbacks,
    verbose=1
)

# =========================
# EVALUATE
# =========================
loss, acc = model.evaluate(X_test, y_test)
print("\nTEST ACCURACY:", acc)

# 🔥 CHI TIẾT HƠN accuracy
y_pred = (model.predict(X_test) > 0.5).astype(int)

print("\nCONFUSION MATRIX:")
print(confusion_matrix(y_test, y_pred))

print("\nCLASSIFICATION REPORT:")
print(classification_report(y_test, y_pred))

# =========================
# SCALER EXPORT
# =========================
print("\n=== SCALER PARAMETERS ===")
print("t_mean =", scaler.mean_[0])
print("h_mean =", scaler.mean_[1])
print("a_mean =", scaler.mean_[2])

print("t_std  =", scaler.scale_[0])
print("h_std  =", scaler.scale_[1])
print("a_std  =", scaler.scale_[2])
print("================================\n")

# =========================
# SANITY CHECK
# =========================
print("\n=== SANITY CHECK ===")

tests = [
    ("NORMAL", [30, 60, 0.1]),
    ("HIGH TEMP", [40, 60, 0.1]),
    ("LOW HUMI", [30, 10, 0.1]),
    ("HIGH HUMI", [30, 80, 0.1]),
    ("ACCEL SPIKE", [30, 60, 3.0]),
    ("BORDERLINE", [35, 70, 1.9])  # 🔥 điểm khó
]

for name, sample in tests:
    pred = model.predict(scaler.transform([sample]))[0][0]
    print(f"{name}: {pred:.4f}")

# =========================
# SAVE MODEL
# =========================
model.save("modelab.keras")

# =========================
# TFLITE (TinyML)
# =========================
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]

tflite_model = converter.convert()

with open("modelab.tflite", "wb") as f:
    f.write(tflite_model)

print("\nDONE -> modelab.tflite")