#ifndef WEB_UI_H
#define WEB_UI_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Military Transport Gateway</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            background:
                radial-gradient(circle at top left, rgba(43,108,176,0.10), transparent 28%),
                linear-gradient(180deg, #f8fbff 0%, #eef4fb 100%);
            color: #2c3e50;
            font-family: 'Segoe UI', sans-serif;
            padding: 20px;
        }
        
        .container {
            max-width: 480px; /* nhỏ lại */
            margin: 0 auto;
            border-radius: 12px;
            background: #ffffff;
            padding: 25px;
            box-shadow: 0 10px 28px rgba(43,108,176,0.10), 0 2px 8px rgba(0,0,0,0.06);
            border: 1px solid #dcdcdc;
        }
        
        .header {
            text-align: center;
            margin-bottom: 20px;
        }
        
        .header h1 {
            font-size: 13px;
            color: #6c757d;
        }
        
        .header p {
            font-size: 13px;
            color: #6c757d;
        }
        
        .device-info {
            margin-bottom: 25px;
            padding: 12px;
            background: #e6fffa;
            border-radius: 8px;
            text-align: center;
        }
        
        .device-info span {
            display: block;
            font-size: 14px;
        }
        
        .device-id {
            color: #2b6cb0;
            font-weight: bold;
            margin-top: 5px;
        }
        
        .section {
            margin-bottom: 25px;
        }
        
        .section-title {
            font-size: 14px;
            letter-spacing: 1px;
            text-transform: uppercase;
            color: #2b6cb0;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 1px solid #cbd5e0;
        }
        
        .scan-button {
            width: 100%;
            padding: 12px 20px;
            background: #ffffff;
            border: 1px solid #2b6cb0;
            color: #2b6cb0;
            font-family: 'Courier New', monospace;
            font-size: 16px;
            cursor: pointer;
            margin-bottom: 15px;
            text-transform: uppercase;
            letter-spacing: 1px;
            transition: all 0.3s;
            border-radius: 6px;
        }
        
        .scan-button:hover {
            background: #2b6cb0;
            color: #ffffff;
        }
        
        .scan-button:active {
            transform: scale(0.98);
        }
        
        .networks-list {
            max-height: 200px;
            overflow-y: auto;
            margin-bottom: 15px;
            border: 1px solid #e0e0e0;
            background: #ffffff;
            padding: 0;
        }
        
        .network-item {
            padding: 12px 15px;
            border-bottom: 1px solid #eee;
            cursor: pointer;
            transition: all 0.2s;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .network-item:last-child {
            border-bottom: none;
        }
        
        .network-item:hover {
            background: #f0f8ff;
            color: #2c3e50;
        }
        
        .network-item.selected {
            background: linear-gradient(90deg, #cfe3ff 0%, #9ec5ff 100%);
            color: #2c3e50;
            border-left: 4px solid #2b6cb0;
            padding-left: 11px;
        }
        
        .network-name {
            font-weight: bold;
            flex: 1;
        }
        
        .network-signal {
            font-size: 12px;
            color: #4a5568;
            margin-right: 10px;
        }
        
        .network-security {
            font-size: 11px;
            color: #e53e3e;
            min-width: 30px;
            text-align: right;
        }
        
        .form-group {
            margin-bottom: 20px;
        }
        
        .form-label {
            display: block;
            font-size: 13px;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 8px;
            color: #4a5568;
        }
        
        .form-input, .form-select {
            width: 100%;
            padding: 12px 15px;
            background: #ffffff;
            border: 1px solid #cbd5e0;
            color: #2c3e50;
            font-size: 14px;
            transition: all 0.3s;
            border-radius: 6px;
        }
        
        .form-input:focus, .form-select:focus {
            outline: none;
            box-shadow: 0 0 5px rgba(43,108,176,0.3);
            background: #ffffff;
            border-color: #2b6cb0;
        }
        
        .form-input::placeholder {
            color: #718096;
            opacity: 0.7;
        }
        
        .form-select option {
            background: #ffffff;
            color: #2c3e50;
        }
        
        .form-buttons {
            display: flex;
            gap: 15px;
            margin-top: 30px;
        }
        
        .btn {
            flex: 1;
            padding: 14px 20px;
            border: 1px solid #2b6cb0;
            background: #ffffff;
            color: #2b6cb0;
            font-size: 16px;
            text-transform: uppercase;
            letter-spacing: 1px;
            cursor: pointer;
            transition: all 0.3s;
            border-radius: 6px;
        }
        
        .btn:hover:not(:disabled) {
            background: #2b6cb0;
            color: #ffffff;
            box-shadow: 0 0 10px rgba(43,108,176,0.3);
        }
        
        .btn:active:not(:disabled) {
            transform: scale(0.98);
        }
        
        .btn:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        
        .btn-primary {
            background: #2b6cb0;
            border: none;
            color: #ffffff;
        }
        
        .btn-primary:hover:not(:disabled) {
            background: #1e4e8c;
            color: #ffffff;
        }
        
        .status-section {
            margin-top: 30px;
            padding-top: 20px;
            border-top: 2px solid #00ff9c;
        }
        
        .status-display {
            padding: 15px;
            background: #f8fafc;
            border: 1px solid #cbd5e0;
            text-align: center;
            font-size: 14px;
        }
        
        .status-idle {
            color: #2b6cb0;
        }
        
        .status-scanning {
            color: #d69e2e;
            animation: blink 1s infinite;
        }
        
        .status-configuring {
            color: #e53e3e;
            animation: blink 0.5s infinite;
        }
        
        .status-success {
            color: #38a169;
        }
        
        @keyframes blink {
            0%, 49% { opacity: 1; }
            50%, 100% { opacity: 0.5; }
        }
        
        .loading {
            display: inline-block;
        }
        
        .spinner {
            display: inline-block;
            width: 12px;
            height: 12px;
            border: 2px solid #2b6cb0;
            border-top: 2px solid transparent;
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
            margin-left: 8px;
        }
        
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
        
        .error-message {
            color: #c53030;
            background: #fff5f5;
            border: 1px solid #feb2b2;
            padding: 12px;
            margin-bottom: 15px;
            display: none;
        }
        
        .error-message.show {
            display: block;
        }
        
        .signal-bars {
            display: inline-block;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>MILITARY TRANSPORT</h1>
            <p>GATEWAY CONFIGURATION</p>
        </div>
        
        <div class="device-info">
            <span>DEVICE</span>
            <span class="device-id" id="deviceId">RX-Gateway</span>
        </div>
        
        <div class="error-message" id="errorMsg"></div>
        
        <div class="section">
            <div class="section-title">AVAILABLE NETWORKS</div>
            <button class="scan-button" id="scanBtn">SCAN WIRELESS</button>
            <button class="scan-button" id="resetBtn" style="background: #fff5f5; border-color: #f6adad; color: #c53030;">RESET WiFi</button>
            <div class="networks-list" id="networksList">
                <div class="network-item" style="justify-content: center; color: #00cc7a;">
                    Click SCAN to find networks...
                </div>
            </div>
        </div>
        
        <form id="configForm">
            <div class="section">
                <div class="form-group">
                    <label class="form-label">SSID</label>
                    <input type="text" class="form-input" id="ssidInput" placeholder="Select or enter SSID" required>
                </div>
                
                <div class="form-group">
                    <label class="form-label">PASSWORD</label>
                    <input type="password" class="form-input" id="passwordInput" placeholder="Enter WiFi password" required minlength="8">
                </div>
                
                <div class="form-buttons">
                    <button type="submit" class="btn btn-primary">CONFIGURE</button>
                </div>
            </div>
        </form>
        
        <div class="status-section">
            <div class="section-title">STATUS</div>
            <div class="status-display status-idle" id="statusDisplay">IDLE</div>
        </div>
    </div>
    
    <script>
        const scanBtn = document.getElementById('scanBtn');
        const networksList = document.getElementById('networksList');
        const ssidInput = document.getElementById('ssidInput');
        const passwordInput = document.getElementById('passwordInput');
        const configForm = document.getElementById('configForm');
        const statusDisplay = document.getElementById('statusDisplay');
        const errorMsg = document.getElementById('errorMsg');
        const deviceId = document.getElementById('deviceId');
        
        let networks = [];
        let isScanning = false;
        
        // Load status on page load
        window.addEventListener('load', () => {
            loadStatus();
        });
        
        // Scan for networks
        scanBtn.addEventListener('click', async () => {
            if (isScanning) return;
            
            isScanning = true;
            scanBtn.disabled = true;
            statusDisplay.textContent = 'SCANNING';
            statusDisplay.className = 'status-display status-scanning';
            errorMsg.classList.remove('show');
            
            try {
                const response = await fetch('/scan');
                const data = await response.json();
                networks = data;
                displayNetworks(data);
                statusDisplay.textContent = 'IDLE';
                statusDisplay.className = 'status-display status-idle';
            } catch (error) {
                showError('Scan failed: ' + error.message);
                statusDisplay.textContent = 'SCAN FAILED';
                statusDisplay.className = 'status-display';
            } finally {
                isScanning = false;
                scanBtn.disabled = false;
            }
        });
        
        // Display scanned networks with signal strength icons
        function displayNetworks(networks) {
            if (networks.length === 0) {
                networksList.innerHTML = '<div class="network-item" style="justify-content: center; color: #00cc7a;">No networks found</div>';
                return;
            }
            
            networksList.innerHTML = networks.map(net => {
                // Signal strength label based on RSSI
                let signalLabel = 'L';
                if (net.rssi > -50) signalLabel = 'Strong';     
                else if (net.rssi > -70) signalLabel = 'Good';  
                else if (net.rssi > -80) signalLabel = 'Weak';   
                else signalLabel = 'Low';                       
                
                return `
                    <div class="network-item" onclick="selectNetwork('${net.ssid.replace(/'/g, "\\'")}')">
                        <span style="flex: 0 0 60px; font-weight: bold; color: #2b6cb0;">${signalLabel}</span>
                        <span class="network-name" style="flex: 1;">${net.ssid}</span>
                        <span class="network-signal">${net.rssi}dBm</span>
                        <span class="network-security">${net.secure ? '🔒' : '🔓'}</span>
                    </div>
                `;
            }).join('');
        }
        
        // Select network
        function selectNetwork(ssid) {
            ssidInput.value = ssid;
            passwordInput.focus();
            
            // Highlight selected
            document.querySelectorAll('.network-item').forEach(item => {
                item.classList.remove('selected');
                if (item.textContent.includes(ssid)) {
                    item.classList.add('selected');
                }
            });
        }
        
        // Configure WiFi
        configForm.addEventListener('submit', async (e) => {
            e.preventDefault();
            
            const ssid = ssidInput.value.trim();
            const password = passwordInput.value;
            
            if (!ssid || !password) {
                showError('SSID and password required');
                return;
            }
            
            if (password.length < 8) {
                showError('Password must be at least 8 characters');
                return;
            }
            
            configBtn.disabled = true;
            statusDisplay.textContent = 'CONFIGURING';
            statusDisplay.className = 'status-display status-configuring';
            errorMsg.classList.remove('show');
            
            try {
                const response = await fetch('/wifi', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({
                        ssid: ssid,
                        password: password
                    })
                });
                
                const result = await response.json();
                
                if (response.ok && result.status === 'saved') {
                    statusDisplay.textContent = 'SUCCESS - RESTARTING';
                    statusDisplay.className = 'status-display status-success';
                    
                    // Disable form
                    configForm.style.opacity = '0.5';
                    configForm.style.pointerEvents = 'none';
                    
                    // Show message
                    errorMsg.textContent = 'Configuration saved. ESP32 restarting...';
                    errorMsg.className = 'error-message show';
                    errorMsg.style.borderColor = '#9ae6b4';
                    errorMsg.style.background = '#f0fff4';
                    errorMsg.style.color = '#2f855a';
                    
                    // Auto redirect after 5 seconds
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 5000);
                } else {
                    showError(result.message || 'Configuration failed');
                    statusDisplay.textContent = 'FAILED';
                    statusDisplay.className = 'status-display';
                }
            } catch (error) {
                showError('Network error: ' + error.message);
                statusDisplay.textContent = 'ERROR';
                statusDisplay.className = 'status-display';
            } finally {
                configBtn.disabled = false;
            }
        });
        
        function showError(msg) {
            errorMsg.textContent = msg;
            errorMsg.className = 'error-message show';
        }
        
        // Reset WiFi Configuration
        const resetBtn = document.getElementById('resetBtn');
        resetBtn.addEventListener('click', async () => {
            if (!confirm('Clear WiFi config and restart? Device will enter AP mode.')) return;
            
            resetBtn.disabled = true;
            statusDisplay.textContent = 'RESETTING';
            statusDisplay.className = 'status-display';
            
            try {
                const response = await fetch('/reset', { method: 'POST' });
                const result = await response.json();
                
                statusDisplay.textContent = 'DEVICE REBOOTING';
                errorMsg.textContent = 'WiFi config cleared. Device restarting to AP mode...';
                errorMsg.className = 'error-message show';
                errorMsg.style.borderColor = '#fed7aa';
                errorMsg.style.background = '#fffaf0';
                errorMsg.style.color = '#b7791f';
                
                // Auto redirect after 8 seconds
                setTimeout(() => {
                    window.location.href = '/';
                }, 8000);
            } catch (error) {
                showError('Reset failed: ' + error.message);
                statusDisplay.textContent = 'RESET FAILED';
                resetBtn.disabled = false;
            }
        });
        
        // Auto scan refresh every 15 seconds
        setInterval(async () => {
            if (!isScanning && networks.length === 0) {
                try {
                    const response = await fetch('/scan');
                    const data = await response.json();
                    if (data.length > 0) {
                        networks = data;
                        displayNetworks(data);
                    }
                } catch (error) {
                    console.log('Auto-scan failed:', error);
                }
            }
        }, 15000);
        
        async function loadStatus() {
            try {
                const response = await fetch('/status');
                const data = await response.json();
                deviceId.textContent = data.device || 'RX-Gateway';
                
                // Show IP and connection info
                if (data.connected) {
                    errorMsg.textContent = `Connected: ${data.ssid} | IP: ${data.ip} | RSSI: ${data.rssi} dBm`;
                    errorMsg.style.borderColor = '#00ff9c';
                    errorMsg.style.background = '#1a4d2e';
                    errorMsg.style.color = '#00ff9c';
                    errorMsg.className = 'error-message show';
                } else if (data.state !== 0) {
                    errorMsg.textContent = `⚠ WiFi Lost - Reconnecting...`;
                    errorMsg.style.borderColor = '#ffaa00';
                    errorMsg.style.background = '#4d3300';
                    errorMsg.style.color = '#ffaa00';
                    errorMsg.className = 'error-message show';
                }
            } catch (error) {
                console.log('Status load failed:', error);
            }
        }
        
        // Get form button reference
        const configBtn = configForm.querySelector('button[type="submit"]');
    </script>
</body>
</html>
)rawliteral";

#endif // WEB_UI_H
