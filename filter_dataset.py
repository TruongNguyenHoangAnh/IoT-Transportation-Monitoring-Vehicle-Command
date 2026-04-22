#!/usr/bin/env python3
"""
Filter dataML.csv to keep only Transport-1 and Transport-2 samples
Output: dataML_filtered.csv (ready for kNN retraining)
"""

import csv
import sys

print("[Filter] Loading dataML.csv...")

# Read CSV and filter
rows = []
vehicle_col_idx = None
vehicle_count = {'Transport-1': 0, 'Transport-2': 0, 'other': 0}
header = None

try:
    with open('dataML.csv', 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        header = reader.fieldnames
        
        # Find vehicle column
        vehicle_cols = [col for col in header if 'vehicle' in col.lower() or 'transport' in col.lower() or col.lower() == 'v']
        if not vehicle_cols:
            print("[Filter] ERROR: Could not find vehicle column!")
            print(f"[Filter] Columns: {header}")
            sys.exit(1)
        
        vehicle_col = vehicle_cols[0]
        print(f"[Filter] Using vehicle column: '{vehicle_col}'")
        
        # Read all rows
        all_rows = list(reader)
        print(f"[Filter] Original size: {len(all_rows)} rows")
        
        # Get unique vehicles
        unique_vehicles = set(row[vehicle_col] for row in all_rows)
        print(f"[Filter] Unique vehicles: {sorted(unique_vehicles)}")
        
        # Filter rows
        for row in all_rows:
            vehicle = row[vehicle_col]
            if vehicle in ['Transport-1', 'Transport-2']:
                rows.append(row)
                vehicle_count[vehicle] += 1
            else:
                vehicle_count['other'] += 1
        
        print(f"[Filter] Filtered size: {len(rows)} rows")
        print(f"[Filter] Transport-1: {vehicle_count['Transport-1']}")
        print(f"[Filter] Transport-2: {vehicle_count['Transport-2']}")
        print(f"[Filter] Other (filtered out): {vehicle_count['other']}")

    # Write filtered CSV
    if len(rows) > 0:
        with open('dataML_filtered.csv', 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=header)
            writer.writeheader()
            writer.writerows(rows)
        
        print(f"\n[Filter] ✓ Saved: dataML_filtered.csv ({len(rows)} samples)")
        print("[Filter] Ready for kNN retraining!")
    else:
        print("[Filter] ERROR: No Transport-1 or Transport-2 found!")
        sys.exit(1)

except Exception as e:
    print(f"[Filter] ERROR: {e}")
    sys.exit(1)
