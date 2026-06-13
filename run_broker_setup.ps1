# ============================================================================
# run_broker_setup.ps1
# Execute TLS setup on broker via SSH
# ============================================================================

param(
    [string]$BrokerIP = "10.77.175.75",
    [string]$BrokerUser = "root",
    [string]$CertDir = "mqtt_tls_certs",
    [string]$SetupScript = "setup_broker_tls.sh"
)

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  REMOTE MOSQUITTO TLS SETUP VIA SSH" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# Verify files exist
Write-Host "`n[CHECK] Verifying local files..." -ForegroundColor Yellow
if (-not (Test-Path $SetupScript)) {
    Write-Host "[ERROR] Setup script not found: $SetupScript" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "$CertDir/ca.crt")) {
    Write-Host "[ERROR] Certificates not found in: $CertDir" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] All files ready" -ForegroundColor Green

# Step 1: Copy setup script to broker
Write-Host "`n[STEP 1] Copying setup script to broker..." -ForegroundColor Yellow
try {
    $remotePath = "/tmp/$SetupScript"
    # Using scp to copy the script
    & scp -o StrictHostKeyChecking=no -o ConnectTimeout=10 $SetupScript "$BrokerUser@$BrokerIP`:$remotePath" 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Setup script copied to $BrokerIP`:$remotePath" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Failed to copy setup script" -ForegroundColor Red
        Write-Host "Make sure SSH is working: ssh $BrokerUser@$BrokerIP" -ForegroundColor Yellow
        exit 1
    }
} catch {
    Write-Host "[ERROR] SCP copy failed: $_" -ForegroundColor Red
    exit 1
}

# Step 2: Copy certificates to broker
Write-Host "`n[STEP 2] Copying certificates to broker..." -ForegroundColor Yellow
try {
    & scp -o StrictHostKeyChecking=no -r $CertDir "$BrokerUser@$BrokerIP`:~/" 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Certificates copied to broker" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Failed to copy certificates" -ForegroundColor Red
        exit 1
    }
} catch {
    Write-Host "[ERROR] Certificates copy failed: $_" -ForegroundColor Red
    exit 1
}

# Step 3: Make setup script executable
Write-Host "`n[STEP 3] Making setup script executable..." -ForegroundColor Yellow
& ssh -o StrictHostKeyChecking=no "$BrokerUser@$BrokerIP" "chmod +x /tmp/$SetupScript" 2>&1 | ForEach-Object { Write-Host $_ }

# Step 4: Execute setup script on broker
Write-Host "`n[STEP 4] Executing setup script on broker..." -ForegroundColor Yellow
Write-Host "This may take a minute..." -ForegroundColor Cyan
try {
    & ssh -o StrictHostKeyChecking=no "$BrokerUser@$BrokerIP" "sudo /tmp/$SetupScript ~/mqtt_tls_certs" 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -eq 0) {
        Write-Host "`n[OK] Setup completed successfully" -ForegroundColor Green
    } else {
        Write-Host "`n[ERROR] Setup script failed" -ForegroundColor Red
        exit 1
    }
} catch {
    Write-Host "[ERROR] SSH execution failed: $_" -ForegroundColor Red
    exit 1
}

# Step 5: Verify setup
Write-Host "`n[STEP 5] Verifying setup..." -ForegroundColor Yellow
& ssh -o StrictHostKeyChecking=no "$BrokerUser@$BrokerIP" "sudo systemctl status mosquitto --no-pager && echo '' && sudo ss -tln | grep -E '1883|8883'" 2>&1 | ForEach-Object { Write-Host $_ }

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "SETUP COMPLETE" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green

Write-Host "`nNext: Test TLS connection from this machine:" -ForegroundColor Cyan
Write-Host "  powershell -ExecutionPolicy Bypass -File test_mosquitto_tls.ps1`n" -ForegroundColor Green
