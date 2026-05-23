#ifndef DASHBOARD_H
#define DASHBOARD_H

const char dashboard_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Thermostat Dashboard</title>
    <style>
        :root {
            --bg: #0a0e14;
            --card-bg: #161b22;
            --text-main: #f0f6fc;
            --text-muted: #8b949e;
            --accent-green: #2ea44f;
            --accent-red: #da3633;
            --accent-amber: #d29922;
            --border-color: #30363d;
            --border-radius: 8px;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg);
            color: var(--text-main);
            margin: 0;
            padding: 20px;
            display: flex;
            justify-content: center;
        }
        .container {
            max-width: 800px;
            width: 100%;
        }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 24px;
            padding-bottom: 16px;
            border-bottom: 1px solid var(--border-color);
        }
        h1 { margin: 0; font-size: 24px; font-weight: 600; }
        
        .status-badge {
            padding: 6px 12px;
            border-radius: 20px;
            font-size: 14px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .status-dot {
            width: 10px; height: 10px; border-radius: 50%;
        }
        .status-heating { background: rgba(46, 164, 79, 0.1); color: var(--accent-green); border: 1px solid var(--accent-green); }
        .status-heating .status-dot { background: var(--accent-green); box-shadow: 0 0 8px var(--accent-green); animation: pulse 2s infinite; }
        .status-idle { background: rgba(139, 148, 158, 0.1); color: var(--text-muted); border: 1px solid var(--text-muted); }
        .status-idle .status-dot { background: var(--text-muted); }
        .status-suspended { background: rgba(210, 153, 34, 0.1); color: var(--accent-amber); border: 1px solid var(--accent-amber); }
        .status-suspended .status-dot { background: var(--accent-amber); }
        
        @keyframes pulse {
            0% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(46, 164, 79, 0.7); }
            70% { transform: scale(1); box-shadow: 0 0 0 6px rgba(46, 164, 79, 0); }
            100% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(46, 164, 79, 0); }
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 24px;
        }
        .card {
            background: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: var(--border-radius);
            padding: 20px;
            display: flex;
            flex-direction: column;
        }
        .card-title {
            font-size: 12px;
            text-transform: uppercase;
            color: var(--text-muted);
            font-weight: 600;
            margin-bottom: 8px;
            letter-spacing: 0.5px;
        }
        .card-value {
            font-size: 28px;
            font-weight: 300;
        }
        .card-value span { font-size: 16px; color: var(--text-muted); }
        
        .controls-card {
            grid-column: 1 / -1;
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
            align-items: center;
        }
        @media (max-width: 600px) {
            .controls-card { grid-template-columns: 1fr; }
        }
        
        input[type=range] {
            -webkit-appearance: none;
            width: 100%;
            background: transparent;
            display: block;
            position: relative;
            z-index: 10;
        }
        input[type=range]::-webkit-slider-thumb {
            -webkit-appearance: none;
            height: 24px;
            width: 24px;
            border-radius: 50%;
            background: var(--accent-green);
            cursor: pointer;
            margin-top: -8px;
            box-shadow: 0 0 10px rgba(46, 164, 79, 0.5);
        }
        input[type=range]::-webkit-slider-runnable-track {
            width: 100%;
            height: 8px;
            cursor: pointer;
            background: #30363d;
            border-radius: 4px;
        }
        
        .mode-toggle {
            display: flex;
            background: #30363d;
            border-radius: var(--border-radius);
            overflow: hidden;
            width: 100%;
        }
        .mode-btn {
            flex: 1;
            padding: 12px;
            border: none;
            background: transparent;
            color: var(--text-muted);
            font-weight: 600;
            cursor: pointer;
            transition: 0.2s;
        }
        .mode-btn.active-heat { background: var(--accent-green); color: white; }
        .mode-btn.active-eco { background: var(--accent-amber); color: white; }
        .mode-btn.active-off { background: var(--accent-red); color: white; }

        .diagnostics-card {
            grid-column: 1 / -1;
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 24px;
            border-top: 3px solid #ff9f43;
        }
        @media (max-width: 600px) {
            .diagnostics-card { grid-template-columns: 1fr; }
        }
        .diag-section-title {
            font-size: 13px;
            font-weight: 700;
            color: #ff9f43;
            margin-bottom: 12px;
            border-bottom: 1px solid #30363d;
            padding-bottom: 6px;
            letter-spacing: 0.5px;
        }
        .diag-grid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 8px;
        }
        .diag-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 12px;
        }
        .diag-label { color: var(--text-muted); }
        .diag-value { font-family: monospace; color: var(--text-main); font-weight: 600; }
        .diag-ok { color: var(--accent-green); }
        .diag-warn { color: var(--accent-amber); }
        .diag-fail { color: var(--accent-red); }

        .footer {
            margin-top: 32px;
            text-align: center;
            color: var(--text-muted);
            font-size: 12px;
        }
    </style>
</head>
<body>

<div class="container">
    <header>
        <h1>Thermostat Dashboard</h1>
        <div id="statusBadge" class="status-badge status-idle">
            <div class="status-dot"></div>
            <span id="statusText">LOADING...</span>
        </div>
    </header>

    <div class="grid">
        <!-- Controls -->
        <div class="card controls-card">
            <div>
                <div class="card-title">Target Temperature</div>
                <div class="card-value" style="margin-bottom: 12px;" id="displayTarget">--<span>°C</span></div>
                <input type="range" id="targetSlider" min="10" max="30" step="0.5" value="22" oninput="updateDisplay(this.value)" onchange="sendTarget(this.value)">
            </div>
            <div>
                <div class="card-title">System Mode</div>
                <div class="mode-toggle">
                    <button id="btnHeat" class="mode-btn" onclick="sendMode('HEAT')">HEAT</button>
                    <button id="btnEco" class="mode-btn" onclick="sendMode('ECO')">ECO</button>
                    <button id="btnOff" class="mode-btn" onclick="sendMode('OFF')">OFF</button>
                </div>
                <div style="margin-top: 12px; font-size: 12px; color: var(--text-muted);" id="decisionReason">Reason: --</div>
            </div>
        </div>

        <!-- Metrics -->
        <div class="card" style="border-left: 3px solid var(--accent-amber);">
            <div class="card-title">Target Temp</div>
            <div class="card-value" id="valTargetCard">--<span>°C</span></div>
            <div style="color: var(--text-muted); font-size: 14px; margin-top: 4px;">Set via Slider / Cloud</div>
        </div>

        <div class="card">
            <div class="card-title">Indoor Actual</div>
            <div class="card-value" id="valIndoor">--<span>°C</span></div>
            <div style="color: var(--text-muted); font-size: 14px; margin-top: 4px;" id="valHum">--<span>% Humidity</span></div>
        </div>
        
        <div class="card" style="border-left: 3px solid var(--accent-green);">
            <div class="card-title">Feels-Like (Apparent)</div>
            <div class="card-value" id="valApparent">--<span>°C</span></div>
        </div>
        
        <div class="card">
            <div class="card-title">Outdoor Temp</div>
            <div class="card-value" id="valOutdoor">--<span>°C</span></div>
            <div style="color: var(--text-muted); font-size: 14px; margin-top: 4px;" id="valHyst">Hysteresis: --</div>
        </div>

        <div class="card">
            <div class="card-title">Estimated Gas Usage</div>
            <div class="card-value" id="valGasToday">--<span>m³ (Today)</span></div>
            <div style="color: var(--text-muted); font-size: 14px; margin-top: 4px;" id="valGasWeek">Weekly: -- m³</div>
        </div>
        
        <div class="card">
            <div class="card-title">Time to Target</div>
            <div class="card-value" id="valTimeToTarget">--<span> mins</span></div>
            <div style="color: var(--text-muted); font-size: 14px; margin-top: 4px;" id="valRunToday">Today's Runtime: -- mins</div>
        </div>

        <!-- System Diagnostics Panel -->
        <div class="card diagnostics-card">
            <div>
                <div class="diag-section-title">BOILER CONTROLLER DIAGNOSTICS</div>
                <div class="diag-grid">
                    <div class="diag-item"><span class="diag-label">IP Address</span><span class="diag-value" id="diagBoilerIp">--</span></div>
                    <div class="diag-item"><span class="diag-label">Free RAM (Heap)</span><span class="diag-value" id="diagBoilerHeap">--</span></div>
                    <div class="diag-item"><span class="diag-label">Reset Reason</span><span class="diag-value" id="diagBoilerReset">--</span></div>
                    <div class="diag-item"><span class="diag-label">Flash Size (Real)</span><span class="diag-value" id="diagBoilerFlash">--</span></div>
                    <div class="diag-item"><span class="diag-label">Wi-Fi Disconnects</span><span class="diag-value" id="diagBoilerWifiDisc">--</span></div>
                    <div class="diag-item"><span class="diag-label">Relay Switch Count</span><span class="diag-value" id="diagBoilerRelayClicks">--</span></div>
                    <div class="diag-item"><span class="diag-label">Window Suspends</span><span class="diag-value" id="diagBoilerWindowTrig">--</span></div>
                    <div class="diag-item"><span class="diag-label">Weather Sync Fails</span><span class="diag-value" id="diagBoilerWeatherFails">--</span></div>
                </div>
            </div>
            <div>
                <div class="diag-section-title">REMOTE SENSOR NODE HEALTH</div>
                <div class="diag-grid">
                    <div class="diag-item"><span class="diag-label">Sensor Status</span><span class="diag-value" id="diagSensorStatus">OFFLINE</span></div>
                    <div class="diag-item"><span class="diag-label">IP Address</span><span class="diag-value" id="diagSensorIp">--</span></div>
                    <div class="diag-item"><span class="diag-label">Signal (RSSI)</span><span class="diag-value" id="diagSensorRssi">--</span></div>
                    <div class="diag-item"><span class="diag-label">Sensor Uptime</span><span class="diag-value" id="diagSensorUptime">--</span></div>
                    <div class="diag-item"><span class="diag-label">Free RAM (Heap)</span><span class="diag-value" id="diagSensorHeap">--</span></div>
                    <div class="diag-item"><span class="diag-label">DHT11 Fail Rate</span><span class="diag-value" id="diagSensorDhtFail">--</span></div>
                    <div class="diag-item"><span class="diag-label">Last Seen</span><span class="diag-value" id="diagSensorLastSeen">--</span></div>
                </div>
            </div>
        </div>
    </div>

    <div class="footer">
        <span id="uptime">Uptime: --</span> | Viessmann Vitopend 100 Integration
    </div>
</div>

<script>
    function updateDisplay(val) {
        document.getElementById('displayTarget').innerHTML = val + '<span>°C</span>';
    }

    function sendTarget(val) {
        fetch('/api/set?target=' + val, { method: 'POST' }).then(fetchData);
    }

    function sendMode(mode) {
        fetch('/api/set?mode=' + mode, { method: 'POST' }).then(fetchData);
    }

    function fetchData() {
        fetch('/api/status')
            .then(res => res.json())
            .then(data => {
                // Update Badge
                const badge = document.getElementById('statusBadge');
                const badgeText = document.getElementById('statusText');
                const relayOn = data.system.relay === "ON";
                const isSuspended = data.system.decision_reason.includes("WINDOW") || data.system.decision_reason.includes("Suspended");
                
                badge.className = 'status-badge';
                if (isSuspended) {
                    badge.classList.add('status-suspended');
                    badgeText.innerText = 'SUSPENDED';
                } else if (relayOn) {
                    badge.classList.add('status-heating');
                    badgeText.innerText = 'ONLINE / HEATING';
                } else {
                    badge.classList.add('status-idle');
                    badgeText.innerText = 'ONLINE / IDLE';
                }
                
                document.getElementById('decisionReason').innerText = "Reason: " + data.system.decision_reason;

                // Update Controls
                const currentSliderVal = document.getElementById('targetSlider').value;
                if (!document.getElementById('targetSlider').matches(':active')) {
                    document.getElementById('targetSlider').value = data.environment.target_temp_c;
                    updateDisplay(data.environment.target_temp_c);
                }

                if (data.system.mode === "HEAT") {
                    document.getElementById('btnHeat').classList.add('active-heat');
                    document.getElementById('btnEco').classList.remove('active-eco');
                    document.getElementById('btnOff').classList.remove('active-off');
                } else if (data.system.mode === "ECO") {
                    document.getElementById('btnEco').classList.add('active-eco');
                    document.getElementById('btnHeat').classList.remove('active-heat');
                    document.getElementById('btnOff').classList.remove('active-off');
                } else {
                    document.getElementById('btnOff').classList.add('active-off');
                    document.getElementById('btnHeat').classList.remove('active-heat');
                    document.getElementById('btnEco').classList.remove('active-eco');
                }

                // Update Metrics
                document.getElementById('valTargetCard').innerHTML = data.environment.target_temp_c.toFixed(1) + '<span>°C</span>';
                document.getElementById('valIndoor').innerHTML = data.environment.indoor_temp_c.toFixed(1) + '<span>°C</span>';
                document.getElementById('valHum').innerHTML = data.environment.indoor_hum_pct.toFixed(0) + '<span>% Humidity</span>';
                document.getElementById('valApparent').innerHTML = data.environment.apparent_temp_c.toFixed(1) + '<span>°C</span>';
                document.getElementById('valOutdoor').innerHTML = data.environment.outdoor_temp_c.toFixed(1) + '<span>°C</span>';
                document.getElementById('valHyst').innerText = "Hysteresis: ±" + data.environment.dynamic_hysteresis_minus.toFixed(2);
                
                document.getElementById('valGasToday').innerHTML = data.analytics.gas_used_m3_today.toFixed(2) + '<span>m³ (Today)</span>';
                document.getElementById('valGasWeek').innerText = "Weekly: " + data.analytics.gas_used_m3_this_week.toFixed(2) + " m³";
                document.getElementById('valTimeToTarget').innerHTML = data.analytics.estimated_mins_to_target + '<span> mins</span>';
                document.getElementById('valRunToday').innerText = "Today's Runtime: " + data.analytics.heating_minutes_today + " mins";

                // Boiler Diagnostics
                document.getElementById('diagBoilerIp').innerText = data.network ? data.network.ip : (data.hardware ? "--" : "--");
                if (data.hardware) {
                    document.getElementById('diagBoilerHeap').innerText = (data.hardware.free_heap_bytes / 1024).toFixed(1) + " KB";
                    document.getElementById('diagBoilerReset').innerText = data.hardware.reset_reason.substring(0, 20);
                    document.getElementById('diagBoilerFlash').innerText = (data.hardware.flash_real_size_bytes / 1024 / 1024).toFixed(0) + " MB";
                }
                if (data.diagnostics) {
                    document.getElementById('diagBoilerWifiDisc').innerText = data.diagnostics.wifi_disconnect_count;
                    document.getElementById('diagBoilerRelayClicks').innerText = data.diagnostics.relay_switch_count;
                    document.getElementById('diagBoilerWindowTrig').innerText = data.diagnostics.open_window_triggers;
                    document.getElementById('diagBoilerWeatherFails').innerText = data.diagnostics.weather_fetch_fails + " / " + data.diagnostics.weather_fetch_attempts;
                }

                // Sensor Diagnostics
                const sensStatus = document.getElementById('diagSensorStatus');
                if (data.sensor_health && data.sensor_health.connected) {
                    sensStatus.innerText = "ONLINE";
                    sensStatus.className = "diag-value diag-ok";
                    
                    document.getElementById('diagSensorIp').innerText = data.sensor_health.ip;
                    document.getElementById('diagSensorRssi').innerText = data.sensor_health.rssi_dbm + " dBm";
                    document.getElementById('diagSensorHeap').innerText = (data.sensor_health.free_heap_bytes / 1024).toFixed(1) + " KB";
                    
                    const dhtFail = data.sensor_health.dht_fail_rate_pct;
                    const dhtEl = document.getElementById('diagSensorDhtFail');
                    dhtEl.innerText = dhtFail.toFixed(1) + "%";
                    if (dhtFail > 10) dhtEl.className = "diag-value diag-fail";
                    else if (dhtFail > 2) dhtEl.className = "diag-value diag-warn";
                    else dhtEl.className = "diag-value diag-ok";
                    
                    const sensUpHrs = Math.floor(data.sensor_health.uptime_seconds / 3600);
                    const sensUpMins = Math.floor((data.sensor_health.uptime_seconds % 3600) / 60);
                    document.getElementById('diagSensorUptime').innerText = sensUpHrs + "h " + sensUpMins + "m";
                    document.getElementById('diagSensorLastSeen').innerText = data.sensor_health.last_seen_seconds_ago + "s ago";
                } else {
                    sensStatus.innerText = "OFFLINE";
                    sensStatus.className = "diag-value diag-fail";
                    
                    document.getElementById('diagSensorIp').innerText = "--";
                    document.getElementById('diagSensorRssi').innerText = "--";
                    document.getElementById('diagSensorHeap').innerText = "--";
                    document.getElementById('diagSensorDhtFail').innerText = "--";
                    document.getElementById('diagSensorDhtFail').className = "diag-value";
                    document.getElementById('diagSensorUptime').innerText = "--";
                    document.getElementById('diagSensorLastSeen').innerText = "--";
                }

                // Footer
                const uptimeHrs = Math.floor(data.system.uptime_seconds / 3600);
                const uptimeMins = Math.floor((data.system.uptime_seconds % 3600) / 60);
                document.getElementById('uptime').innerText = "Uptime: " + uptimeHrs + "h " + uptimeMins + "m";
            })
            .catch(err => {
                const badge = document.getElementById('statusBadge');
                badge.className = 'status-badge status-suspended';
                document.getElementById('statusText').innerText = 'OFFLINE';
            });
    }

    setInterval(fetchData, 5000);
    fetchData(); // Initial load
</script>
</body>
</html>
)=====";

#endif
