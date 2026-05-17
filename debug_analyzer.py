import re

with open('esp32_real_log.txt', 'r', errors='ignore') as f:
    lines = f.readlines()

pattern = r'\[TIMING\].*?Inference:\s+([\d.]+)\s*ms.*?Avg:\s+([\d.]+)\s*ms.*?Min:\s+([\d.]+)\s*ms.*?Max:\s+([\d.]+)\s*ms.*?Count:\s+(\d+)'
matches = 0
for line in lines:
    if re.search(pattern, line):
        matches += 1

print(f'Total lines: {len(lines)}')
print(f'Pattern matches with full regex: {matches}')
print(f'Lines with [TIMING] string: {sum(1 for l in lines if "[TIMING]" in l)}')

# Show first 3 lines with TIMING
print('\nFirst 3 [TIMING] lines:')
count = 0
for line in lines:
    if '[TIMING]' in line:
        print(repr(line[:150]))
        # Try to match this specific line
        if re.search(pattern, line):
            print("  -> MATCHES pattern")
        else:
            print("  -> DOES NOT match pattern")
        count += 1
        if count >= 3:
            break
