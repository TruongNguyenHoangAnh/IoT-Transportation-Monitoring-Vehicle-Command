import numpy as np

with open("model_nn.tflite", "rb") as f:
    data = f.read()

# =========================
# FORMAT HEX (12 bytes/line)
# =========================
hex_lines = []

for i in range(0, len(data), 12):
    chunk = data[i:i+12]
    line = ", ".join(f"0x{b:02x}" for b in chunk)
    hex_lines.append("  " + line)

hex_array = ",\n".join(hex_lines)

# =========================
# GENERATE HEADER
# =========================
c_array = f"""#ifndef MODEL_H
#define MODEL_H

const unsigned char model_tflite[] = {{
{hex_array}
}};

const unsigned int model_tflite_len = {len(data)};

#endif
"""

with open("model_nn.h", "w") as f:
    f.write(c_array)

print("DONE -> model_nn.h")