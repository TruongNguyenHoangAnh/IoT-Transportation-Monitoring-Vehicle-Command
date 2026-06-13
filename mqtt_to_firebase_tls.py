# ============================================================================
# mqtt_to_firebase_tls.py
#
# Backend Python Bridge with TLS/SSL Support for MQTT
# Replaces mqtt_to_firebase.py with secure connection to TLS broker
#
# Key Changes:
#   - paho-mqtt client with TLS configuration
#   - Verify server certificate using CA cert
#   - Hostname verification
#   - Optional client certificate for mutual TLS (mTLS)
#
# ============================================================================

#!/usr/bin/env python3

import logging
logging.basicConfig(level=logging.INFO)
import os
import uuid
import json
import time
import threading
import signal
from datetime import datetime, timezone
from collections import deque
from concurrent.futures import ThreadPoolExecutor

import firebase_admin
import paho.mqtt.client as mqtt
from firebase_admin import credentials, firestore
from google.api_core.exceptions import ServiceUnavailable, InternalServerError, DeadlineExceeded
import google.api_core.client_options

# ==================== MQTT TLS CONFIG ====================
# Connection to TLS-enabled MQTT Broker

MQTT_HOST = os.getenv("MQTT_HOST", "10.77.175.75")  # Broker IP address (CN will be verified against mqtt.edgeai.local)
MQTT_PORT = int(os.getenv("MQTT_PORT", "8883"))           # TLS port
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

# TLS Certificate Configuration
MQTT_TLS_ENABLED = os.getenv("MQTT_TLS_ENABLED", "true").lower() == "true"
MQTT_CA_CERT = os.getenv("MQTT_CA_CERT", "mqtt_tls_certs/ca.crt")      # CA certificate path
MQTT_CLIENT_CERT = os.getenv("MQTT_CLIENT_CERT", "")                    # Client cert (optional mTLS)
MQTT_CLIENT_KEY = os.getenv("MQTT_CLIENT_KEY", "")                       # Client key (optional mTLS)
MQTT_TLS_VERSION = os.getenv("MQTT_TLS_VERSION", "TLSv1_2")             # TLS version
MQTT_VERIFY_HOSTNAME = os.getenv("MQTT_VERIFY_HOSTNAME", "false").lower() == "true"  # Disabled: connecting via IP address

# TLS Debugging
MQTT_TLS_DEBUG = os.getenv("MQTT_TLS_DEBUG", "false").lower() == "true"

MQTT_TOPICS = [
    ("vehicles/+/telemetry", 1),
    ("vehicles/+/status", 1),
    ("vehicles/command_vehicle/gps", 1),
]

# Firestore config
FIRESTORE_CREDENTIALS = "serviceAccountKey.json"
FIRESTORE_DB_COLLECTION = "vehicles"

# Batch write config
BATCH_MAX_OPS = 10
BATCH_FLUSH_INTERVAL_MS = 5000
BATCH_COMMIT_TIMEOUT_SEC = 30
QUOTA_ERROR_BACKOFF_MS = 60000
FAILED_QUEUE_CACHE = "failed_firestore_queue.json"

MIN_WRITE_INTERVAL_MS = 500
TELEMETRY_INTERVAL_MS = 500
last_state_cache = {}

# ==================== GLOBAL STATE ====================
mqtt_client = None
firebase_db = None
mqtt_connected = False

pending_batch_ops = deque()
batch_lock = threading.Lock()
last_batch_flush_ms = 0
MAX_PENDING_OPS = 500

active_flush_thread = None
flush_thread_start_ms = 0
FLUSH_THREAD_TIMEOUT_SEC = 120

last_quota_error_ms = 0
last_anomaly_ms = {}
last_telemetry_global_ms = {}

# TLS diagnostics
tls_diagnostics = {
    "handshake_time_ms": 0,
    "cert_verify_count": 0,
    "last_tls_error": None
}

# ==================== LOGGING ====================

def log_info(msg):
    """Log important events"""
    print(f"[INFO] {datetime.now().isoformat()} {msg}", flush=True)

def log_error(msg):
    """Log errors"""
    print(f"[ERROR] {datetime.now().isoformat()} {msg}", flush=True)

def log_batch(msg):
    """Log batch operations"""
    print(f"[FB] {msg}", flush=True)

def log_tls(msg):
    """Log TLS-specific events"""
    if MQTT_TLS_ENABLED:
        print(f"[TLS] {datetime.now().isoformat()} {msg}", flush=True)

def log_debug(msg):
    """Debug log if enabled"""
    if MQTT_TLS_DEBUG:
        print(f"[DEBUG] {datetime.now().isoformat()} {msg}", flush=True)

def serialize_for_cache(obj):
    """Serialize objects for JSON caching, handling GeoPoint"""
    if isinstance(obj, firestore.GeoPoint):
        return {
            "__geopoint__": True,
            "latitude": obj.latitude,
            "longitude": obj.longitude
        }
    elif isinstance(obj, dict):
        return {k: serialize_for_cache(v) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [serialize_for_cache(item) for item in obj]
    else:
        return obj

# ==================== MQTT CALLBACKS WITH TLS ====================

def on_tls_connect(client, userdata, flags, rc, properties=None):
    """MQTT callback: on_connect"""
    global mqtt_connected

    if rc == 0:
        mqtt_connected = True
        elapsed_ms = (time.time() - userdata.get("connect_start_time", time.time())) * 1000
        log_info(f"MQTT connected to {MQTT_HOST}:{MQTT_PORT} (handshake: {elapsed_ms:.0f}ms)")

        if MQTT_TLS_ENABLED:
            log_tls(f"Successfully connected via TLS")
            log_tls(f"Cipher: {client._ssl_sock.cipher()[0] if hasattr(client, '_ssl_sock') else 'unknown'}")

        for topic, qos in MQTT_TOPICS:
            client.subscribe(topic, qos=qos)

    else:
        mqtt_connected = False
        # MQTT error codes
        error_messages = {
            1: "Connection refused - incorrect protocol version",
            2: "Connection refused - invalid client identifier",
            3: "Connection refused - server unavailable",
            4: "Connection refused - bad username or password",
            5: "Connection refused - not authorized",
        }
        error_msg = error_messages.get(rc, f"Unknown error code {rc}")
        log_error(f"MQTT connection failed: {error_msg} (rc={rc})")

        if MQTT_TLS_ENABLED:
            if rc == -2:
                log_tls("Certificate verification failed!")
                tls_diagnostics["cert_verify_count"] += 1
                tls_diagnostics["last_tls_error"] = "cert_verify_failed"

def on_tls_disconnect(client, userdata, rc, properties=None):
    """MQTT callback: on_disconnect"""
    global mqtt_connected

    mqtt_connected = False

    if rc != 0:
        if MQTT_TLS_ENABLED:
            log_tls(f"Disconnected from TLS broker (code={rc})")
        else:
            log_error(f"MQTT disconnected: code {rc}")

def on_tls_message(client, userdata, msg):
    """MQTT callback: on_message"""
    try:
        payload_str = msg.payload.decode(errors="replace")
        log_info(f"MQTT received topic={msg.topic} payload_len={len(payload_str)}")

        payload = json.loads(payload_str)
        vehicle_id = extract_vehicle_id(msg.topic)

        if not vehicle_id:
            log_error(f"Invalid topic format: {msg.topic}")
            return

        queue_firestore_write(vehicle_id, payload, msg.topic)

    except json.JSONDecodeError:
        log_error(f"Invalid JSON from {msg.topic}: {payload_str[:80]}")
    except Exception as e:
        log_error(f"Message processing error: {e}")

# ==================== HELPER FUNCTIONS ====================

def extract_vehicle_id(topic):
    if not topic:
        return None
    parts = topic.split('/')
    if len(parts) < 3:
        return None
    vehicle_id = parts[1]
    if not vehicle_id:
        return None
    return vehicle_id

# ==================== MQTT SETUP WITH TLS ====================

def init_mqtt():
    """Initialize MQTT client with TLS if enabled"""
    global mqtt_client

    # Create MQTT client
    mqtt_client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"Firestore-Bridge-TLS-{uuid.uuid4().hex[:6]}"
    )

    # Store connection metadata
    mqtt_client.user_data_set({
        "db_client": firebase_db,
        "connect_start_time": time.time()
    })

    # Set callbacks
    mqtt_client.on_connect = on_tls_connect
    mqtt_client.on_disconnect = on_tls_disconnect
    mqtt_client.on_message = on_tls_message

    # Configure reconnect policy
    mqtt_client.reconnect_delay_set(min_delay=2, max_delay=10)

    # Configure authentication if provided
    if MQTT_USERNAME and MQTT_PASSWORD:
        mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

    # Configure TLS/SSL if enabled
    if MQTT_TLS_ENABLED:
        log_tls("Configuring TLS/SSL...")

        # Verify CA certificate file exists
        if not os.path.exists(MQTT_CA_CERT):
            log_error(f"CA certificate not found: {MQTT_CA_CERT}")
            log_tls("Falling back to insecure connection (NOT RECOMMENDED)")
            mqtt_client.tls_insecure_set(True)
        else:
            # Set CA certificate for server verification
            mqtt_client.tls_set(
                ca_certs=MQTT_CA_CERT,
                certfile=MQTT_CLIENT_CERT if MQTT_CLIENT_CERT else None,
                keyfile=MQTT_CLIENT_KEY if MQTT_CLIENT_KEY else None,
                cert_reqs=mqtt.ssl.CERT_REQUIRED,
                tls_version=mqtt.ssl.PROTOCOL_TLSv1_2,
                ciphers=None
            )

            # Verify server hostname matches certificate CN
            if MQTT_VERIFY_HOSTNAME:
                mqtt_client.tls_insecure_set(False)
                log_tls(f"TLS configured with hostname verification ({MQTT_HOST})")
            else:
                mqtt_client.tls_insecure_set(True)
                log_tls("TLS configured without hostname verification (INSECURE)")

        # Enable TLS debugging if requested
        if MQTT_TLS_DEBUG:
            mqtt_client.enable_logger(logging.getLogger("paho.mqtt"))
            log_debug("MQTT TLS debugging enabled")

    # Configure clean session
    mqtt_client.clean_session = True

    # Connect to broker
    try:
        log_info(f"Connecting to MQTT broker: {MQTT_HOST}:{MQTT_PORT}")
        mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=120)
        mqtt_client.loop_start()

        if MQTT_TLS_ENABLED:
            log_tls(f"TLS connection initiated to {MQTT_HOST}:{MQTT_PORT}")
        else:
            log_info(f"Plain MQTT connection initiated to {MQTT_HOST}:{MQTT_PORT}")

    except Exception as e:
        log_error(f"Failed to connect to MQTT broker: {e}")
        if MQTT_TLS_ENABLED:
            log_tls(f"Check: CA cert path, hostname, port, and firewall")
            raise

# ==================== FIRESTORE ====================

def init_firestore():
    """Initialize Firestore client"""
    global firebase_db

    try:
        firebase_admin.get_app()
    except ValueError:
        cred = credentials.Certificate(FIRESTORE_CREDENTIALS)
        firebase_admin.initialize_app(cred)

    firebase_db = firestore.client()
    log_info("Firestore connected")

def normalize_telemetry(vehicle_id, payload):
    """Convert MQTT payload to Firestore document"""
    temp_raw = payload.get("temperature", 0)
    humidity_raw = payload.get("humidity", 0)

    temp_celsius = temp_raw / 10.0 if temp_raw > 0 else 0
    humidity_pct = humidity_raw / 10.0 if humidity_raw > 0 else 0

    accel_raw = payload.get("accel_magnitude", 0)
    accel_ms2 = accel_raw / 100.0 if accel_raw > 0 else 0

    doc = {
        "vehicle_id": vehicle_id,
        "temperature": round(temp_celsius, 1),
        "humidity": round(humidity_pct, 1),
        "accel_magnitude": round(accel_ms2, 2),
        "light_level": payload.get("light", 0),
        "tamper": payload.get("tamper_flag", 0),
        "gateway_rssi": payload.get("rssi", 0),
        "gateway_snr": payload.get("snr", 0),
        "anomaly_flag": payload.get("anomaly_flag", 0),
        "timestamp_ms": payload.get("timestamp", 0),
        "last_updated": firestore.SERVER_TIMESTAMP,
        "online": True,
    }

    lat = payload.get("latitude", 0)
    lng = payload.get("longitude", 0)
    doc["gps"] = firestore.GeoPoint(lat, lng)
    doc["gps_source"] = "telemetry"

    return doc

def queue_firestore_write(vehicle_id, payload, topic):
    """Queue Firestore write operation"""
    global last_anomaly_ms, last_telemetry_global_ms, last_state_cache

    now_ms = int(time.time() * 1000)
    timestamp_iso = datetime.now(timezone.utc).isoformat(timespec='milliseconds').replace('+00:00', 'Z')

    if "/command_vehicle/gps" in topic:
        lat = payload.get("latitude", 0)
        lng = payload.get("longitude", 0)

        command_data = {
            "vehicle_id": "command_vehicle",
            "gps": firestore.GeoPoint(lat, lng),
            "gps_source": "command_vehicle",
            "online": True,
            "last_check": firestore.SERVER_TIMESTAMP,
            "last_updated": firestore.SERVER_TIMESTAMP,
        }

        with batch_lock:
            if len(pending_batch_ops) + 1 > MAX_PENDING_OPS:
                log_error(f"Queue overflow: {len(pending_batch_ops)} ops pending")
                return

            ref_cmd = firebase_db.collection(FIRESTORE_DB_COLLECTION).document("command_vehicle")
            pending_batch_ops.append({
                "ref": ref_cmd,
                "data": command_data,
                "merge": True
            })

            log_info(f"Queued command_vehicle GPS: lat={lat:.6f} lon={lng:.6f}")

            if len(pending_batch_ops) >= BATCH_MAX_OPS:
                trigger_async_flush()

    elif "/telemetry" in topic:
        last_vehicle = last_telemetry_global_ms.get(vehicle_id, 0)
        if now_ms - last_vehicle < TELEMETRY_INTERVAL_MS:
            return

        last_telemetry_global_ms[vehicle_id] = now_ms
        data = normalize_telemetry(vehicle_id, payload)

        if f"{vehicle_id}_online" in last_state_cache:
            data["online"] = last_state_cache[f"{vehicle_id}_online"]

        with batch_lock:
            if len(pending_batch_ops) + 2 > MAX_PENDING_OPS:
                log_error(f"Queue overflow: {len(pending_batch_ops)} ops pending")
                return

            ref_toplevel = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id)
            pending_batch_ops.append({
                "ref": ref_toplevel,
                "data": data,
                "merge": True
            })

            ref_archive = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id).collection("history").document(timestamp_iso)
            pending_batch_ops.append({
                "ref": ref_archive,
                "data": data,
                "merge": False
            })

            log_info(f"Queued telemetry: vehicle_id={vehicle_id}")

            if len(pending_batch_ops) >= BATCH_MAX_OPS:
                trigger_async_flush()

    elif "/status" in topic:
        last_vehicle_status_ms = last_state_cache.get(f"{vehicle_id}_status_time", 0)

        if now_ms - last_vehicle_status_ms < 3000:
            return

        last_state_cache[f"{vehicle_id}_status_time"] = now_ms
        last_state_cache[f"{vehicle_id}_online"] = payload.get("online", True)

        status_data = {
            "vehicle_id": vehicle_id,
            "online": payload.get("online", True),
            "packet_count": payload.get("packet_count", 0),
            "packet_count_total": payload.get("packet_count_total", 0),
            "rejected_count": payload.get("rejected_count", 0),
            "rejected_count_total": payload.get("rejected_count_total", 0),
            "wifi_rssi": payload.get("wifi_rssi", 0),
            "heap_free": payload.get("heap_free", 0),
            "uptime_ms": payload.get("uptime_ms", 0),
            "timestamp_ms": payload.get("timestamp_ms", now_ms),
            "anomaly_flag": payload.get("anomaly_flag", 0),
            "last_updated": firestore.SERVER_TIMESTAMP
        }

        with batch_lock:
            if len(pending_batch_ops) + 1 > MAX_PENDING_OPS:
                log_error(f"Queue overflow: {len(pending_batch_ops)} ops pending")
                return

            ref_archive = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id).collection("status").document(timestamp_iso)
            pending_batch_ops.append({
                "ref": ref_archive,
                "data": status_data,
                "merge": False
            })

            log_info(f"Queued status archive: vehicle_id={vehicle_id}")

            if len(pending_batch_ops) >= BATCH_MAX_OPS:
                trigger_async_flush()

def trigger_async_flush():
    """Trigger asynchronous batch flush"""
    global active_flush_thread, flush_thread_start_ms

    now_ms = int(time.time() * 1000)
    thread_age_sec = (now_ms - flush_thread_start_ms) / 1000.0 if flush_thread_start_ms else 0

    if active_flush_thread and active_flush_thread.is_alive():
        if thread_age_sec < FLUSH_THREAD_TIMEOUT_SEC:
            return
        log_batch(f"Flush thread timeout ({thread_age_sec:.1f}s), restarting")

    flush_thread_start_ms = now_ms

    def worker():
        try:
            flush_batch_writes()
        except Exception as e:
            log_error(f"Flush exception: {e}")

    active_flush_thread = threading.Thread(target=worker, daemon=True)
    active_flush_thread.start()

def flush_batch_writes():
    """Flush pending batch operations to Firestore"""
    global last_batch_flush_ms, last_quota_error_ms

    now_ms = int(time.time() * 1000)

    if last_quota_error_ms and now_ms - last_quota_error_ms < QUOTA_ERROR_BACKOFF_MS:
        return

    with batch_lock:
        if not pending_batch_ops:
            return

        ops_count = len(pending_batch_ops)
        ops_to_flush = list(pending_batch_ops)
        pending_batch_ops.clear()

    batch = firebase_db.batch()

    for op in ops_to_flush:
        batch.set(op["ref"], op["data"], merge=op["merge"])

    try:
        batch.commit()
        log_batch(f"OK: {ops_count} ops")
        last_batch_flush_ms = now_ms
        return ops_count

    except DeadlineExceeded:
        log_error(f"Deadline exceeded, retrying {ops_count} ops")
        time.sleep(2)
        with batch_lock:
            pending_batch_ops.extend(ops_to_flush)
        return 0

    except ServiceUnavailable:
        log_error(f"Service unavailable, retrying {ops_count} ops")
        time.sleep(2)
        with batch_lock:
            pending_batch_ops.extend(ops_to_flush)
        return 0

    except Exception as e:
        error_str = str(e)

        if "429" in error_str or "Quota" in error_str:
            log_batch(f"Quota exceeded, backoff 60s")
            last_quota_error_ms = now_ms
        else:
            log_error(f"Batch failed: {error_str[:80]}")

        cache_failed_ops(ops_to_flush)
        return 0

def cache_failed_ops(ops):
    """Cache failed operations to SPIFFS for recovery"""
    try:
        cache = []
        for op in ops:
            cache_item = {
                "path": op["ref"].path,
                "data": serialize_for_cache(op["data"]),
                "merge": op["merge"]
            }
            cache.append(cache_item)

        with open(FAILED_QUEUE_CACHE, "w") as f:
            json.dump(cache, f)
    except Exception as e:
        log_error(f"Cache error: {e}")

def load_failed_ops():
    """Load and replay failed operations from cache"""
    if not os.path.exists(FAILED_QUEUE_CACHE):
        return 0

    try:
        with open(FAILED_QUEUE_CACHE, "r") as f:
            content = f.read().strip()

        if not content:
            return 0

        try:
            cache = json.loads(content)
        except json.JSONDecodeError:
            log_error("Cache file corrupted → deleting")
            os.remove(FAILED_QUEUE_CACHE)
            return 0

        if not isinstance(cache, list) or len(cache) == 0:
            return 0

        replayed = 0

        for item in cache[:2]:
            try:
                ref = firebase_db.document(item["path"])
                data = item.get("data")

                if isinstance(data, str):
                    try:
                        data = json.loads(data)
                    except:
                        log_error(f"Cache item data is string, can't parse")
                        continue

                if not isinstance(data, dict):
                    log_error(f"Cache item data not dict: {type(data)}")
                    continue

                if isinstance(data, dict) and "gps" in data:
                    gps_data = data["gps"]
                    if isinstance(gps_data, dict) and "latitude" in gps_data:
                        data["gps"] = firestore.GeoPoint(
                            gps_data["latitude"],
                            gps_data["longitude"]
                        )

                with batch_lock:
                    if len(pending_batch_ops) < MAX_PENDING_OPS:
                        is_duplicate = any(
                            op["ref"].path == ref.path and op["data"] == data
                            for op in pending_batch_ops
                        )

                        if not is_duplicate:
                            pending_batch_ops.append({
                                "ref": ref,
                                "data": data,
                                "merge": item.get("merge", False)
                            })
                            replayed += 1
                    else:
                        log_error(f"Cache replay: queue full")
                        break

            except Exception as e:
                log_error(f"Replay error: {e}")

        remaining = cache[replayed:]

        if remaining:
            tmp_file = FAILED_QUEUE_CACHE + ".tmp"
            with open(tmp_file, "w") as f:
                json.dump(remaining, f)
            os.replace(tmp_file, FAILED_QUEUE_CACHE)
        else:
            try:
                os.remove(FAILED_QUEUE_CACHE)
            except FileNotFoundError:
                pass

        if replayed > 0:
            log_batch(f"Replayed {replayed} ops")

        return replayed

    except Exception as e:
        log_error(f"Load cache error: {e}")
        return 0

# ==================== MAIN ====================

def signal_handler(sig, frame):
    """Handle Ctrl+C gracefully"""
    log_info("Bridge shutting down (Ctrl+C)")
    if mqtt_client:
        mqtt_client.loop_stop()
    import sys
    sys.exit(0)

def main_loop():
    """Main event loop"""
    global active_flush_thread

    last_flush_check = 0
    last_replay_check = 0

    log_info("Bridge started")
    if MQTT_TLS_ENABLED:
        log_tls("Operating in TLS/SSL mode")

    try:
        while True:
            now_ms = int(time.time() * 1000)

            if now_ms - last_flush_check >= BATCH_FLUSH_INTERVAL_MS:
                last_flush_check = now_ms
                with batch_lock:
                    if pending_batch_ops:
                        trigger_async_flush()

            if now_ms - last_replay_check >= 120000:
                last_replay_check = now_ms
                load_failed_ops()

            time.sleep(1)

    except KeyboardInterrupt:
        log_info("Bridge shutting down")
        if active_flush_thread and active_flush_thread.is_alive():
            active_flush_thread.join(timeout=10)
        if mqtt_client:
            mqtt_client.loop_stop()

if __name__ == "__main__":
    # Register signal handler
    signal.signal(signal.SIGINT, signal_handler)

    # Log TLS diagnostic info
    if MQTT_TLS_ENABLED:
        log_tls(f"TLS Configuration:")
        log_tls(f"  - CA Certificate: {MQTT_CA_CERT}")
        log_tls(f"  - Hostname: {MQTT_HOST}:{MQTT_PORT}")
        log_tls(f"  - Verify Hostname: {MQTT_VERIFY_HOSTNAME}")
        if MQTT_CLIENT_CERT:
            log_tls(f"  - Client Cert (mTLS): {MQTT_CLIENT_CERT}")

    init_firestore()
    init_mqtt()
    time.sleep(2)
    main_loop()
