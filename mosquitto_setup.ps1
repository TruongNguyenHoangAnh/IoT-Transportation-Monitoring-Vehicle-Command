# ============================================================================
# mosquitto_setup.ps1
# Deploy Mosquitto TLS configuration and certificates
# ============================================================================

$BROKER_IP = "10.77.175.75"
$BROKER_USER = "root"  # Change if needed
$CERT_DIR = "mqtt_tls_certs"
$REMOTE_CERT_DIR = "/etc/mosquitto/certs"
$REMOTE_CONFIG_DIR = "/etc/mosquitto"

Write-Host "========== MOSQUITTO TLS SETUP ==========" -ForegroundColor Cyan

# Step 1: Check if certificates exist
Write-Host "`n[1] Checking certificates..." -ForegroundColor Yellow
if (-not (Test-Path "$CERT_DIR/ca.crt")) {
    Write-Host "[ERROR] CA certificate not found: $CERT_DIR/ca.crt" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "$CERT_DIR/server.crt")) {
    Write-Host "[ERROR] Server certificate not found: $CERT_DIR/server.crt" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "$CERT_DIR/server.key")) {
    Write-Host "[ERROR] Server key not found: $CERT_DIR/server.key" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] All certificates found" -ForegroundColor Green

# Step 2: Check broker connectivity
Write-Host "`n[2] Checking broker connectivity..." -ForegroundColor Yellow
try {
    $socket = New-Object System.Net.Sockets.TcpClient
    $connection = $socket.BeginConnect($BROKER_IP, 1883, $null, $null)
    $wait = $connection.AsyncWaitHandle.WaitOne(3000)
    
    if ($socket.Connected) {
        Write-Host "[OK] Broker is reachable on port 1883" -ForegroundColor Green
        Write-Host "     Broker IP: $BROKER_IP" -ForegroundColor Green
        $socket.Close()
    } else {
        Write-Host "[WARNING] Broker not responding on port 1883" -ForegroundColor Yellow
    }
} catch {
    Write-Host "[WARNING] Cannot connect to broker: $_" -ForegroundColor Yellow
}

# Step 3: Show deployment instructions
Write-Host "`n[3] Deployment Instructions:" -ForegroundColor Yellow
Write-Host "
On the broker machine ($BROKER_IP), run these commands:

1. Create certificate directory (if not exists):
   sudo mkdir -p $REMOTE_CERT_DIR
   sudo chown mosquitto:mosquitto $REMOTE_CERT_DIR

2. Set secure permissions on certificates:
   sudo chmod 600 $REMOTE_CERT_DIR/server.key
   sudo chmod 644 $REMOTE_CERT_DIR/server.crt
   sudo chmod 644 $REMOTE_CERT_DIR/ca.crt

3. Backup existing config:
   sudo cp $REMOTE_CONFIG_DIR/mosquitto.conf $REMOTE_CONFIG_DIR/mosquitto.conf.bak

4. Update mosquitto.conf to include TLS listener:
   Add these lines to the end of $REMOTE_CONFIG_DIR/mosquitto.conf:

   listener 8883
   protocol mqtt
   cafile $REMOTE_CERT_DIR/ca.crt
   certfile $REMOTE_CERT_DIR/server.crt
   keyfile $REMOTE_CERT_DIR/server.key
   tls_version tlsv1.2
   allow_anonymous true

5. Test Mosquitto config:
   sudo mosquitto -c $REMOTE_CONFIG_DIR/mosquitto.conf -t

6. Restart Mosquitto service:
   sudo systemctl restart mosquitto
   
7. Check service status:
   sudo systemctl status mosquitto
   journalctl -u mosquitto -n 20

8. Verify TLS port is listening:
   sudo netstat -tln | grep 8883
   OR
   sudo ss -tln | grep 8883
" -ForegroundColor Cyan

# Step 4: Show certificate info
Write-Host "`n[4] Certificate Information:" -ForegroundColor Yellow
Write-Host "CA Certificate:     $CERT_DIR/ca.crt"
Write-Host "Server Certificate: $CERT_DIR/server.crt"
Write-Host "Server Key:         $CERT_DIR/server.key"

# Show cert details if openssl available
$opensslPath = (Get-Command openssl.exe -ErrorAction SilentlyContinue).Source
if ($opensslPath) {
    Write-Host "`n[5] Certificate Details:" -ForegroundColor Yellow
    Write-Host "CA Certificate CN:" -NoNewline
    openssl x509 -in "$CERT_DIR/ca.crt" -noout -subject 2>$null
    
    Write-Host "Server Certificate CN:" -NoNewline
    openssl x509 -in "$CERT_DIR/server.crt" -noout -subject 2>$null
    
    Write-Host "Server Certificate SAN:" -NoNewline
    openssl x509 -in "$CERT_DIR/server.crt" -noout -ext subjectAltName 2>$null
    
    Write-Host "`nCA Certificate Valid Until:"
    openssl x509 -in "$CERT_DIR/ca.crt" -noout -enddate
    
    Write-Host "Server Certificate Valid Until:"
    openssl x509 -in "$CERT_DIR/server.crt" -noout -enddate
}

# Step 5: Instructions for copying files
Write-Host "`n[6] To Copy Certificates to Broker:" -ForegroundColor Yellow
Write-Host "
If you have SSH/SCP access to broker, use commands like:

  scp -r mqtt_tls_certs root@10.77.175.75:~/

  ssh root@10.77.175.75
  sudo cp -r ~/mqtt_tls_certs/* /etc/mosquitto/certs/
  sudo chown mosquitto:mosquitto /etc/mosquitto/certs/*
  sudo chmod 600 /etc/mosquitto/certs/server.key
  sudo systemctl restart mosquitto

Or manually copy files to the broker and place in /etc/mosquitto/certs
" -ForegroundColor Gray

Write-Host "`n[7] After Broker TLS is Enabled:" -ForegroundColor Yellow
Write-Host "
Test connection from this machine:
  
  openssl s_client -connect 10.77.175.75:8883 -CAfile mqtt_tls_certs/ca.crt

Expected: Should see 'Verify return code: 0 (ok)'

Then start Python backend:
  
  python mqtt_to_firebase_tls.py

And monitor ESP32:
  
  platformio device monitor -p COM9 -b 115200
" -ForegroundColor Cyan

Write-Host "`n========== SETUP INSTRUCTIONS COMPLETE ==========" -ForegroundColor Green
