#!/usr/bin/env python3

import logging
logging.basicConfig(level=logging.INFO)
import os
import uuid
import json
import time
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
MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

MQTT_TOPICS = [
    ("vehicles/+/telemetry", 1),
    ("vehicles/+/status", 1),
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

MIN_WRITE_INTERVAL_MS = 2000  
TELEMETRY_INTERVAL_MS = 10000  
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

# ==================== LOGGING ====================

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
    """Process incoming MQTT message"""
    
    try:
        payload_str = msg.payload.decode(errors="replace")
        log_info(f"MQTT received topic={msg.topic} payload={payload_str[:200]}")
        payload = json.loads(payload_str)
        vehicle_id = extract_vehicle_id(msg.topic)

        if not vehicle_id:
            log_error(f"Invalid topic format, cannot extract vehicle_id from {msg.topic}")
            return
        
        queue_firestore_write(
            vehicle_id,
            payload,
            msg.topic
        )

    except json.JSONDecodeError:
        log_error(f"Invalid JSON from {msg.topic}: {payload_str[:80]}")
    except Exception as e:
        log_error(f"Message error: {e}")

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

def init_mqtt():
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
    
    mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=120)
    mqtt_client.loop_start()
    log_info(f"MQTT client started (broker={MQTT_HOST}:{MQTT_PORT})")

def mqtt_loop():
    pass

# ==================== FIRESTORE ====================
def init_firestore():
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
    }
    
    lat = payload.get("latitude", 0)
    lng = payload.get("longitude", 0)
    doc["gps"] = firestore.GeoPoint(lat, lng)
    doc["gps_source"] = "telemetry"
    
    return doc


def queue_firestore_write(vehicle_id, payload, topic):
    global last_anomaly_ms, last_telemetry_global_ms, last_state_cache

    now_ms = int(time.time() * 1000)
    data = None
    merge = False
    ref = None

    if "/status" in topic:
        last_state = last_state_cache.get(vehicle_id)
        if last_state == payload:
            return
        last_state_cache[vehicle_id] = payload
        data = payload
        merge = False
        # Create new document in status subcollection with auto-generated ID
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id).collection("status").document()

    elif "/telemetry" in topic:
        last_vehicle = last_telemetry_global_ms.get(vehicle_id, 0)
        if now_ms - last_vehicle < TELEMETRY_INTERVAL_MS:  # 10s per vehicle
            return
        last_telemetry_global_ms[vehicle_id] = now_ms
        data = normalize_telemetry(vehicle_id, payload)
        merge = True
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id)
        
        # AUTO-CREATE STATUS from telemetry every 30s
        if now_ms - last_telemetry_global_ms.get(vehicle_id + "_status_auto", 0) >= 30000:
            last_telemetry_global_ms[vehicle_id + "_status_auto"] = now_ms
            status_data = {
                "vehicle_id": vehicle_id,
                "online": True,
                "packet_count": payload.get("packet_count", 0),
                "packet_count_total": payload.get("packet_count_total", 0),
                "rejected_count": payload.get("rejected_count", 0),
                "rejected_count_total": payload.get("rejected_count_total", 0),
                "wifi_rssi": payload.get("wifi_rssi", 0),
                "heap_free": payload.get("heap_free", 0),
                "uptime_ms": payload.get("uptime_ms", 0),
                "timestamp_ms": payload.get("timestamp_ms", int(time.time() * 1000)),
                "anomaly_flag": payload.get("anomaly_flag", 0),
                "last_updated": firestore.SERVER_TIMESTAMP
            }
            try:
                # Create status document in subcollection with auto-generated ID
                status_ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id).collection("status").document()
                status_ref.set(status_data)
                
                # AUTO-UPDATE /status/lastest for quick latest access
                lastest_status_ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id).collection("status").document("lastest")
                lastest_status_ref.set(status_data)
            except Exception as e:
                print(f"[ERROR] Auto-create status failed for {vehicle_id}: {e}")
        
    else:
        data = payload
        merge = False
        ref = firebase_db.collection(FIRESTORE_DB_COLLECTION).document(vehicle_id)
    
    if data is None or ref is None:
        log_error(f"Cannot queue Firestore write for topic={topic}")
        return

    with batch_lock:
        if len(pending_batch_ops) >= MAX_PENDING_OPS:
            log_error(f"Queue overflow: {len(pending_batch_ops)} ops pending")
            return
        
        pending_batch_ops.append({
            "ref": ref,
            "data": data,
            "merge": merge
        })
        log_info(f"Queued Firestore op: vehicle_id={vehicle_id} topic={topic} pending_ops={len(pending_batch_ops)}")

        if len(pending_batch_ops) >= BATCH_MAX_OPS:
            trigger_async_flush()

def trigger_async_flush():
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

        # Quota exceeded
        if "429" in error_str or "Quota" in error_str:
            log_batch(f"Quota exceeded, backoff 60s")
            last_quota_error_ms = now_ms
        else:
            log_error(f"Batch failed: {error_str[:80]}")

        cache_failed_ops(ops_to_flush)
        return 0

def cache_failed_ops(ops):
    try:
        cache = []
        for op in ops:
            cache_item = {
                "path": op["ref"].path,
                "data": op["data"],  # Keep as dict, will auto-convert
                "merge": op["merge"]
            }
            cache.append(cache_item)
        
        with open(FAILED_QUEUE_CACHE, "w") as f:
            json.dump(cache, f)
    except TypeError as e:
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