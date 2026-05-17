import numpy as np
import pandas as pd

np.random.seed(42)

# =========================
# CONFIG
# =========================
N = 20000
normal_ratio = 0.7

# =========================
# NORMAL DATA
# =========================
def gen_normal(ac=True):

    # =========================
    # TEMPERATURE
    # =========================
    if ac:
        t = np.random.normal(24, 1.5)
    else:
        t = np.random.normal(28, 2.0)

    # =========================
    # HUMIDITY (VN realistic)
    # =========================
    h = np.random.normal(68 - 0.2 * (t - 25), 7.0)

    # =========================
    # ACCELERATION STATES
    # =========================
    r = np.random.rand()

    # đứng yên hoàn toàn
    if r < 0.25:
        a = np.random.normal(0.02, 0.02)

    # rung nhẹ do quạt / bàn / môi trường
    elif r < 0.50:
        a = np.random.normal(0.12, 0.05)

    # xe chạy bình thường
    elif r < 0.75:
        a = np.random.normal(0.45, 0.18)

    # đường xấu / rung mạnh nhưng vẫn NORMAL
    elif r < 0.92:
        a = np.random.normal(0.90, 0.25)

    # bump ngắn nhưng chưa anomaly
    else:
        a = np.random.normal(1.30, 0.20)

    # =========================
    # HARD NORMAL SAMPLES
    # =========================
    if np.random.rand() < 0.20:
        t = np.random.uniform(33, 35)

    if np.random.rand() < 0.20:
        h = np.random.uniform(72, 82)

    # =========================
    # CLIP
    # =========================
    t = np.clip(t, 18, 35)
    h = np.clip(h, 35, 85)
    a = np.clip(a, 0.0, 1.6)

    return t, h, a


# =========================
# ANOMALY DATA
# =========================
def gen_anomaly():

    mode = np.random.choice([
        "HIGH_TEMP",
        "LOW_HUMI",
        "HIGH_HUMI",
        "ACCEL_SPIKE",
        "COMBO"
    ])

    # =========================
    # HIGH TEMPERATURE
    # =========================
    if mode == "HIGH_TEMP":

        t = np.random.uniform(36, 45)
        h = np.random.uniform(40, 70)

        # nhiệt cao nhưng xe bình thường
        a = np.random.uniform(0.2, 1.2)

    # =========================
    # LOW HUMIDITY
    # =========================
    elif mode == "LOW_HUMI":

        t = np.random.uniform(22, 32)
        h = np.random.uniform(10, 25)
        a = np.random.uniform(0.0, 1.0)

    # =========================
    # HIGH HUMIDITY
    # =========================
    elif mode == "HIGH_HUMI":

        t = np.random.uniform(22, 32)
        h = np.random.uniform(85, 100)
        a = np.random.uniform(0.2, 1.2)

    # =========================
    # STRONG IMPACT / FALL / COLLISION
    # =========================
    elif mode == "ACCEL_SPIKE":

        t = np.random.uniform(22, 32)
        h = np.random.uniform(45, 75)

        # va chạm / rơi / rung cực mạnh
        a = np.random.uniform(1.8, 3.5)

    # =========================
    # COMBINED ANOMALY
    # =========================
    else:

        t = np.random.uniform(36, 45)
        h = np.random.uniform(10, 25)
        a = np.random.uniform(2.0, 5.0)

    return t, h, a


# =========================
# GENERATE DATASET
# =========================
data = []

for _ in range(N):

    # NORMAL
    if np.random.rand() < normal_ratio:

        ac_mode = np.random.rand() < 0.6
        t, h, a = gen_normal(ac=ac_mode)
        label = "NORMAL"

    # ANOMALY
    else:

        t, h, a = gen_anomaly()
        label = "ANOMALY"

    # =========================
    # AMBIGUITY ZONE
    # =========================

    # accel vùng xám
    if 1.4 <= a <= 2.0:
        if np.random.rand() < 0.25:
            label = "ANOMALY" if label == "NORMAL" else "NORMAL"

    # temp vùng xám
    if 34 <= t <= 37:
        if np.random.rand() < 0.20:
            label = "ANOMALY" if label == "NORMAL" else "NORMAL"

    # humidity vùng xám
    if 80 <= h <= 88:
        if np.random.rand() < 0.20:
            label = "ANOMALY" if label == "NORMAL" else "NORMAL"

    data.append([t, h, a, label])


# =========================
# DATAFRAME
# =========================
df = pd.DataFrame(
    data,
    columns=["t", "h", "a", "anomaly_label"]
)

# shuffle
df = df.sample(frac=1).reset_index(drop=True)

# =========================
# SAVE
# =========================
df.to_csv("iot_lab_dataset.csv", index=False)

# =========================
# INFO
# =========================
print("DONE")
print(df.head())

print("\nSTATS:")
print(df.describe())

print("\nCLASS DISTRIBUTION:")
print(df["anomaly_label"].value_counts())

print("\nNORMAL SAMPLE:")
print(df[df["anomaly_label"] == "NORMAL"].head(10))

print("\nANOMALY SAMPLE:")
print(df[df["anomaly_label"] == "ANOMALY"].head(10))