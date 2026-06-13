#!/bin/bash
# ============================================================================
# setup_broker_tls.sh
# Automatic Mosquitto TLS Setup for Broker
# Run this on the broker machine: bash setup_broker_tls.sh
# ============================================================================

set -e  # Exit on error

CERT_DIR="${1:-.}"  # Directory containing certificates (default: current dir)
REMOTE_CERT_DIR="/etc/mosquitto/certs"
REMOTE_CONFIG="/etc/mosquitto/mosquitto.conf"

echo "========================================================"
echo "  MOSQUITTO TLS SETUP - AUTOMATED"
echo "========================================================"
echo ""

# Step 1: Check if running as root
echo "[STEP 1] Checking permissions..."
if [ "$EUID" -ne 0 ]; then
   echo "[ERROR] This script must be run as root (use sudo)"
   exit 1
fi
echo "[OK] Running as root"
echo ""

# Step 2: Check if certificates exist
echo "[STEP 2] Verifying certificates..."
if [ ! -f "$CERT_DIR/ca.crt" ]; then
    echo "[ERROR] CA certificate not found: $CERT_DIR/ca.crt"
    exit 1
fi
if [ ! -f "$CERT_DIR/server.crt" ]; then
    echo "[ERROR] Server certificate not found: $CERT_DIR/server.crt"
    exit 1
fi
if [ ! -f "$CERT_DIR/server.key" ]; then
    echo "[ERROR] Server key not found: $CERT_DIR/server.key"
    exit 1
fi
echo "[OK] All certificates found:"
echo "    - $CERT_DIR/ca.crt"
echo "    - $CERT_DIR/server.crt"
echo "    - $CERT_DIR/server.key"
echo ""

# Step 3: Create certificate directory
echo "[STEP 3] Creating certificate directory..."
sudo mkdir -p "$REMOTE_CERT_DIR"
sudo chown mosquitto:mosquitto "$REMOTE_CERT_DIR"
sudo chmod 755 "$REMOTE_CERT_DIR"
echo "[OK] Certificate directory created: $REMOTE_CERT_DIR"
echo ""

# Step 4: Copy certificates
echo "[STEP 4] Copying certificates..."
sudo cp "$CERT_DIR/ca.crt" "$REMOTE_CERT_DIR/"
sudo cp "$CERT_DIR/server.crt" "$REMOTE_CERT_DIR/"
sudo cp "$CERT_DIR/server.key" "$REMOTE_CERT_DIR/"

# Set proper permissions
sudo chmod 600 "$REMOTE_CERT_DIR/server.key"
sudo chmod 644 "$REMOTE_CERT_DIR/server.crt"
sudo chmod 644 "$REMOTE_CERT_DIR/ca.crt"
sudo chown mosquitto:mosquitto "$REMOTE_CERT_DIR"/*

echo "[OK] Certificates copied with correct permissions"
ls -la "$REMOTE_CERT_DIR/"
echo ""

# Step 5: Backup mosquitto.conf
echo "[STEP 5] Backing up mosquitto.conf..."
if [ -f "$REMOTE_CONFIG" ]; then
    sudo cp "$REMOTE_CONFIG" "$REMOTE_CONFIG.bak.$(date +%s)"
    echo "[OK] Backup created: $REMOTE_CONFIG.bak"
else
    echo "[WARNING] $REMOTE_CONFIG not found"
fi
echo ""

# Step 6: Check if TLS listener already exists
echo "[STEP 6] Checking for existing TLS configuration..."
if grep -q "listener 8883" "$REMOTE_CONFIG" 2>/dev/null; then
    echo "[WARNING] TLS listener already configured in mosquitto.conf"
    echo "[INFO] Skipping configuration update"
else
    echo "[INFO] Adding TLS listener to mosquitto.conf..."
    
    # Append TLS config
    sudo tee -a "$REMOTE_CONFIG" > /dev/null << 'EOF'

# ============================================================================
# TLS Configuration - Added by setup_broker_tls.sh
# ============================================================================

# TLS Listener (encrypted MQTT over SSL/TLS on port 8883)
listener 8883
protocol mqtt
cafile /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/server.crt
keyfile /etc/mosquitto/certs/server.key
tls_version tlsv1.2
allow_anonymous true

# Plain MQTT Listener (legacy support on port 1883)
listener 1883
protocol mqtt
allow_anonymous true

# ============================================================================
EOF
    
    echo "[OK] TLS configuration added to mosquitto.conf"
fi
echo ""

# Step 7: Test Mosquitto configuration
echo "[STEP 7] Testing Mosquitto configuration..."
if sudo mosquitto -c "$REMOTE_CONFIG" -t; then
    echo "[OK] Configuration syntax is valid"
else
    echo "[ERROR] Configuration test failed!"
    exit 1
fi
echo ""

# Step 8: Restart Mosquitto
echo "[STEP 8] Restarting Mosquitto service..."
sudo systemctl restart mosquitto

# Wait a moment for service to restart
sleep 2

# Check if service is running
if sudo systemctl is-active --quiet mosquitto; then
    echo "[OK] Mosquitto service restarted successfully"
else
    echo "[ERROR] Mosquitto service failed to start"
    echo "[INFO] Service logs:"
    sudo journalctl -u mosquitto -n 20
    exit 1
fi
echo ""

# Step 9: Verify TLS port is listening
echo "[STEP 9] Verifying TLS port is listening..."
if sudo ss -tln | grep -q ":8883"; then
    echo "[OK] Port 8883 (TLS) is listening"
    sudo ss -tln | grep 8883
else
    echo "[ERROR] Port 8883 is not listening"
    echo "[INFO] Checking service status:"
    sudo systemctl status mosquitto --no-pager
    exit 1
fi
echo ""

# Step 10: Verify plain MQTT port too
echo "[STEP 10] Verifying plain MQTT port..."
if sudo ss -tln | grep -q ":1883"; then
    echo "[OK] Port 1883 (plain MQTT) is listening"
    sudo ss -tln | grep 1883
else
    echo "[WARNING] Port 1883 is not listening (this is OK if not needed)"
fi
echo ""

# Step 11: Show certificate info
echo "[STEP 11] Certificate Information:"
echo "CA Certificate:"
sudo openssl x509 -in "$REMOTE_CERT_DIR/ca.crt" -noout -subject
sudo openssl x509 -in "$REMOTE_CERT_DIR/ca.crt" -noout -enddate

echo ""
echo "Server Certificate:"
sudo openssl x509 -in "$REMOTE_CERT_DIR/server.crt" -noout -subject
sudo openssl x509 -in "$REMOTE_CERT_DIR/server.crt" -noout -ext subjectAltName
sudo openssl x509 -in "$REMOTE_CERT_DIR/server.crt" -noout -enddate
echo ""

# Step 12: Show recent logs
echo "[STEP 12] Recent Mosquitto Logs:"
sudo journalctl -u mosquitto -n 10 --no-pager
echo ""

# Final summary
echo "========================================================"
echo "  ✓ MOSQUITTO TLS SETUP COMPLETED SUCCESSFULLY"
echo "========================================================"
echo ""
echo "Summary:"
echo "  - Certificates: $REMOTE_CERT_DIR"
echo "  - Config: $REMOTE_CONFIG"
echo "  - Plain MQTT: Port 1883"
echo "  - TLS MQTT: Port 8883"
echo ""
echo "Next Steps:"
echo "  1. Test from client machine:"
echo "     openssl s_client -connect localhost:8883 -CAfile ca.crt"
echo ""
echo "  2. Monitor logs:"
echo "     journalctl -u mosquitto -f"
echo ""
echo "========================================================"
