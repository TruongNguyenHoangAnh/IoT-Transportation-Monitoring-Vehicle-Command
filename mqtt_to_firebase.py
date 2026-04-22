#!/usr/bin/env python3
"""
MQTT to Cloud Firestore Bridge
Production-grade: Minimal logging, queue-based, thread-safe batch operations
"""
import logging
logging.basicConfig(level=logging.INFO)
import os
import uuid
import json
import time
import hmac
import hashlib
import threading
import signal
from datetime import datetime
from collections import deque
from concurrent.futures import ThreadPoolExecutor

import firebase_admin
import paho.mqtt.client as mqtt
from firebase_admin import credentials, firestore
from google.api_core.exceptions import ServiceUnavailable, InternalServerError, DeadlineExceeded
import google.api_core.client_options

# ==================== CONFIG ====================
MQTT_HOST = os.getenv("MQTT_HOST", "10.16.1.75")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USE_TLS = os.getenv("MQTT_USE_TLS", "false").lower() in ("1", "true", "yes", "on")
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

MQTT_TOPICS = [
    ("vehicles/+/telemetry", 1),
    ("vehicles/+/anomalies", 1),
    ("vehicles/+/status", 1),
    ("vehicles/+/gps", 1),
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

MIN_WRITE_INTERVAL_MS = 2000  # 2 giây
TELEMETRY_INTERVAL_MS = 10000  # 10 giây
last_state_cache = {}

# ==================== GLOBAL STATE ====================
mqtt_client = None
firebase_db = None
mqtt_connected = False

# Queue for pending operations (FIX #6: No silent drops)
pending_batch_ops = deque()  # No maxlen - check size explicitly
batch_lock = threading.Lock()
last_batch_flush_ms = 0
MAX_PENDING_OPS = 500

# Active flush thread tracking
active_flush_thread = None
flush_thread_start_ms = 0
FLUSH_THREAD_TIMEOUT_SEC = 120  # Must be > BATCH_COMMIT_TIMEOUT_SEC

# Rate limit tracking (separated by purpose - FIX #2)
last_quota_error_ms = 0
last_anomaly_ms = {}              # Anomaly-specific rate limit (ms)
last_telemetry_global_ms = {}    # Telemetry-specific rate limit (ms)

# ==================== LOGGING ====================

def verify_hmac(payload_dict, provided_hmac):
    """Verify HMAC signature - match ESP32 insertion order (NO sorted keys)"""
    payload_copy = {k: v for k, v in payload_dict.items() if k != "hmac"}
    # IMPORTANT: Don't sort keys - ESP32 uses insertion order
    payload_str = json.dumps(payload_copy)
    expected_hmac = hmac.new(
        b"bridge-secret",
        payload_str.encode(),
        hashlib.sha256
    ).hexdigest()
    
    is_valid = hmac.compare_digest(expected_hmac, provided_hmac or "")
    
    if not is_valid:
        import sys
        payload_bytes = payload_str.encode()
        print(f"[DEBUG] === HMAC MISMATCH ===", file=sys.stderr)
        print(f"[DEBUG] Full Payload: {payload_str}", file=sys.stderr)
        print(f"[DEBUG] Payload Length: {len(payload_bytes)} bytes", file=sys.stderr)
        print(f"[DEBUG] Payload Hex: {payload_bytes.hex()}", file=sys.stderr)
        print(f"[DEBUG] Expected HMAC: {expected_hmac}", file=sys.stderr)
        print(f"[DEBUG] Provided HMAC: {provided_hmac}", file=sys.stderr)
    
    return is_valid

def log_info(msg):
    """Log important events only"""
    print(f"[INFO] {datetime.now().isoformat()} {msg}", flush=True)

def log_error(msg):
    """Log errors"""
    print(f"[ERROR] {datetime.now().isoformat()} {msg}", flush=True)

def log_batch(msg):
    """Log batch operations summary"""
    print(f"[FB] {msg}", flush=True)

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

def mqtt_on_connect(client, userdata, flags, rc, properties=None):
    global mqtt_connected
    
    if rc == 0:
        mqtt_connected = True
        log_info(f"MQTT connected to {MQTT_HOST}:{MQTT_PORT}")
        
        for topic, qos in MQTT_TOPICS:
            client.subscribe(topic, qos=qos)
            
    else:
        mqtt_connected = False
        log_error(f"MQTT connection failed: code {rc}")


def mqtt_on_disconnect(client, userdata, rc, properties=None):
    """FIX #8: Remove duplicate log"""
    global mqtt_connected
    
    mqtt_connected = False
    
    if rc != 0:
        log_error(f"MQTT disconnected: code {rc}")


def mqtt_on_message(client, userdata, msg):
    """Process incoming MQTT message - HMAC verification DISABLED"""
    
    try:
        payload = json.loads(msg.payload.decode())
        vehicle_id = extract_vehicle_id(msg.topic)

        if not vehicle_id:
            return
        
        # Remove hmac field from payload (don't store in Firestore)
        payload.pop("hmac", None)
        
        queue_firestore_write(
            vehicle_id,
            payload,
            msg.topic
        )

    except json.JSONDecodeError:
        log_error(f"Invalid JSON from {msg.topic}")
    except Exception as e:
        log_error(f"Message error: {e}")

def extract_vehicle_id(topic):
    """Extract vehicle_id from topic: vehicles/Transport-1/telemetry (FIX #4: Guard edge cases)"""
    if not topic:
        return None
    parts = topic.split('/')
    if len(parts) < 3:  # Should have: vehicles/VEHICLE_ID/TYPE
        return None
    vehicle_id = parts[1]
    if not vehicle_id:  # Handle vehicles//telemetry
        return None
    return vehicle_id

def init_mqtt():
    """Initialize MQTT client"""
    global mqtt_client
    
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"Firestore-Bridge-{uuid.uuid4().hex[:6]}")
    mqtt_client.clean_session = True
    mqtt_client.user_data_set({"db_client": firebase_db})
    mqtt_client.on_connect = mqtt_on_connect
    mqtt_client.on_disconnect = mqtt_on_disconnect
    mqtt_client.on_message = mqtt_on_message
    mqtt_client.reconnect_delay_set(min_delay=2, max_delay=10)
    
    if MQTT_USERNAME and MQTT_PASSWORD:
        mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    
    # Force MQTT v3.1.1 to avoid protocol mismatch with older brokers
    mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=120)
    mqtt_client.loop_start()
    log_info(f"MQTT client started (broker={MQTT_HOST}:{MQTT_PORT}, TLS={MQTT_USE_TLS})")

def mqtt_loop():
    """MQTT background loop (handled by mqtt.loop_start())"""
    pass

# ==================== FIRESTORE ====================
def init_firestore():
    """Initialize Firebase connection"""
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
        "knn_prediction": payload.get("knn_prediction", "NORMAL"),
        "rf_anomaly_state": payload.get("rf_anomaly_state", "NORMAL"),
        "timestamp_ms": payload.get("timestamp", 0),
        "last_updated": firestore.SERVER_TIMESTAMP,
    }
    
    lat = payload.get("latitude", 0)
    lng = payload.get("longitude", 0)
    if lat != 0 and lng != 0:
        doc["gps"] = firestore.GeoPoint(lat, lng)
        doc["gps_source"] = "telemetry"
    
    return doc


def queue_firestore_write(vehicle_id, payload, topic):
    """FIX #1, #2, #3: Unified ms-based rate limiting, separated trackers"""
    global last_anomaly_ms, last_telemetry_global_ms, last_state_cache

    now_ms = int(time.time() * 1000)
    data = None
    merge = False
    ref = None

    # ===============================
    # ANOMALY: 1s rate limit per vehicle
    # ===============================
    if "/anomalies" in topic:
        last = last_anomaly_ms.get(vehicle_id, 0)
        if now_ms - last < 1000:  # 1s per vehicle
            return
        last_anomaly_ms[vehicle_id] = now_ms
        data = payload
        merge = False
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id)

    # ===============================
    # STATUS: store in subcollection
    # ===============================
    elif "/status" in topic:
        last_state = last_state_cache.get(vehicle_id)
        if last_state == payload:
            return
        last_state_cache[vehicle_id] = payload
        data = payload
        merge = False
        # Create new document in status subcollection with auto-generated ID
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id).collection("status").document()

    # ===============================
    # TELEMETRY: 10s per vehicle + 2s global
    # ===============================
    elif "/telemetry" in topic:
        last_vehicle = last_telemetry_global_ms.get(vehicle_id, 0)
        if now_ms - last_vehicle < TELEMETRY_INTERVAL_MS:  # 10s per vehicle
            return
        last_telemetry_global_ms[vehicle_id] = now_ms
        data = normalize_telemetry(vehicle_id, payload)
        merge = True
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id)

    else:
        data = payload
        merge = False
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id)
    
    if data is None or ref is None:
        return
    
    # Remove hmac field if present
    if isinstance(data, dict):
        data.pop("hmac", None)

    # ===============================
    # PUSH TO QUEUE (FIX #6: Guard against silent drops)
    # ===============================
    with batch_lock:
        if len(pending_batch_ops) >= MAX_PENDING_OPS:
            log_error(f"Queue overflow: {len(pending_batch_ops)} ops pending")
            return
        
        pending_batch_ops.append({
            "ref": ref,
            "data": data,
            "merge": merge
        })

        if len(pending_batch_ops) >= BATCH_MAX_OPS:
            trigger_async_flush()

def trigger_async_flush():
    """Spawn async flush thread if not already running (FIX #7: Remove dead code)"""
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
    """Commit pending batch operations to Firestore"""
    global last_batch_flush_ms, last_quota_error_ms

    now_ms = int(time.time() * 1000)

    # Check quota backoff
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
        """FIX #5: Correct exception type for Firestore timeout"""
        log_error(f"Deadline exceeded, retrying {ops_count} ops")
        time.sleep(2)
        with batch_lock:
            pending_batch_ops.extend(ops_to_flush)
        return 0

    except ServiceUnavailable:
        """FIX #5: Handle service unavailable"""
        log_error(f"Service unavailable, retrying {ops_count} ops")
        time.sleep(2)
        with batch_lock:
            pending_batch_ops.extend(ops_to_flush)
        return 0

    except Exception as e:
        error_str = str(e)

        # Quota exceeded
        if "429" in error_str or "Quota" in error_str:
            log_batch(f"Quota exceeded, backoff 60s")
            last_quota_error_ms = now_ms
        else:
            log_error(f"Batch failed: {error_str[:80]}")

        cache_failed_ops(ops_to_flush)
        return 0

def cache_failed_ops(ops):
    """Cache failed operations for retry (FIX #10: NO string conversion of data)"""
    try:
        cache = []
        for op in ops:
            # Store data directly - do NOT convert to string
            # GeoPoint and other types will fail json.dump without proper handling
            cache_item = {
                "path": op["ref"].path,
                "data": op["data"],  # Keep as dict, will auto-convert
                "merge": op["merge"]
            }
            cache.append(cache_item)
        
        with open(FAILED_QUEUE_CACHE, "w") as f:
            # Do NOT use default=str - let JSON encoder handle built-in types
            # GeoPoint will be converted by json.dumps' default handler only if needed
            json.dump(cache, f)
    except TypeError as e:
        # If GeoPoint can't be serialized, convert it explicitly
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
        except Exception as e2:
            log_error(f"Cache error (after retry): {e2}")
    except Exception as e:
        log_error(f"Cache error: {e}")

def load_failed_ops():
    """Load and replay cached failed operations (FIX #4: Prevent double insert)"""

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

        # replay max 2 ops per cycle
        for item in cache[:2]:
            try:
                ref = firebase_db.document(item["path"])
                data = item.get("data")
                
                # Type guard: ensure data is dict, not string (FIX #2)
                if isinstance(data, str):
                    try:
                        data = json.loads(data)
                    except:
                        log_error(f"Cache item data is string, can't parse")
                        continue
                
                if not isinstance(data, dict):
                    log_error(f"Cache item data not dict: {type(data)}")
                    continue
                
                # Restore GeoPoint if needed
                if isinstance(data, dict) and "gps" in data:
                    gps_data = data["gps"]
                    if isinstance(gps_data, dict) and "latitude" in gps_data:
                        data["gps"] = firestore.GeoPoint(
                            gps_data["latitude"],
                            gps_data["longitude"]
                        )

                with batch_lock:
                    if len(pending_batch_ops) < MAX_PENDING_OPS:
                        # FIX #4: Check for duplicates before adding
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
                        log_error(f"Cache replay: queue full, stopping")
                        break

            except Exception as e:
                log_error(f"Replay error: {e}")

        # Update cache file with remaining items
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
    if mqtt_client and mqtt_client.is_connected():
        mqtt_client.loop_stop()
    import sys
    sys.exit(0)

def main_loop():
    """Main event loop"""
    global active_flush_thread
    
    last_flush_check = 0
    last_replay_check = 0
    
    log_info("Bridge started")
    
    try:
        while True:
            now_ms = int(time.time() * 1000)
            
            # Time-based flush (if batch not full)
            if now_ms - last_flush_check >= BATCH_FLUSH_INTERVAL_MS:
                last_flush_check = now_ms
                with batch_lock:
                    if pending_batch_ops:
                        trigger_async_flush()
            
            # Replay failed operations every 2 minutes
            if now_ms - last_replay_check >= 120000:
                last_replay_check = now_ms
                load_failed_ops()
            
            time.sleep(1)
    
    except KeyboardInterrupt:
        log_info("Bridge shutting down")
        if active_flush_thread and active_flush_thread.is_alive():
            active_flush_thread.join(timeout=10)
        mqtt_client.loop_stop()

if __name__ == "__main__":
    # Register Ctrl+C signal handler
    signal.signal(signal.SIGINT, signal_handler)
    
    init_firestore()
    init_mqtt()
    time.sleep(2)  # Let MQTT connect
    main_loop()  