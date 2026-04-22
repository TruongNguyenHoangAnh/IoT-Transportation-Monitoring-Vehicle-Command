#!/usr/bin/env python3
import pandas as pd
from pathlib import Path

FEATURE_COLUMNS = [
    'rssi', 'snr', 'temperature', 'humidity', 'accel_magnitude', 'latitude', 'longitude'
]

def main():
    base = Path('d:/RX_Gateway/RX_ESP32_Firestore')
    df = pd.read_csv(base / 'dataML.csv')
    amb = pd.read_csv(base / 'ambiguous_samples.csv')
    print('full total', len(df), 'ambiguous total', len(amb))
    print('full label counts')
    print(df['label'].value_counts().to_dict())
    print('ambiguous label counts')
    print(amb['label'].value_counts().to_dict())
    print('\n=== FEATURE SUMMARY ===')
    for name in FEATURE_COLUMNS:
        print('\nFEATURE', name)
        print(' full mean,std,min,max', df[name].mean(), df[name].std(), df[name].min(), df[name].max())
        print(' amb mean,std,min,max', amb[name].mean(), amb[name].std(), amb[name].min(), amb[name].max())
        print(' mean diff', amb[name].mean() - df[name].mean())
    print('\n=== AMBIGUOUS normal vs anomaly ===')
    for name in FEATURE_COLUMNS:
        g = amb.groupby('label')[name].agg(['mean', 'std', 'min', 'max', 'count'])
        print('\n', name)
        print(g.to_string())
    print('\n=== feature separations in ambiguous samples (smallest first) ===')
    seps = []
    for name in FEATURE_COLUMNS:
        means = amb.groupby('label')[name].mean()
        if set(means.index) == {'ANOMALY', 'NORMAL'}:
            seps.append((abs(means['ANOMALY'] - means['NORMAL']), name, means.to_dict()))
    seps.sort()
    for sep, name, means in seps:
        print(name, sep, means)

if __name__ == '__main__':
    main()
