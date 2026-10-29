import json
import queue
import threading
import time
import uuid
import paho.mqtt.client as mqtt
import pandas as pd
import streamlit as st

# Konfigurasi Tampilan Web Dashboard
st.set_page_config(
    page_title="Smart Room Dashboard - Syafiq", page_icon="🛡️", layout="wide"
)

st.title("🛡️ Smart Room Telemetry Dashboard (Local)")
st.markdown(
    "Monitoring real-time ESP32 Wokwi menggunakan HiveMQ MQTT Broker (TCP Port"
    " 1883)."
)


# 1. Gunakan st.cache_resource agar Queue & MQTT Client bersifat Singleton (survive antar rerun)
@st.cache_resource
def get_mqtt_queue():
  return queue.Queue(maxsize=100)


data_queue = get_mqtt_queue()

MQTT_HOST = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_DATA = "sic8/smartroom/syafiq/data"
TOPIC_STATUS = "sic8/smartroom/syafiq/status"


# 2. Fungsi background thread dengan reconnect otomatis
def background_mqtt_worker():
  def enqueue(item):
    try:
      data_queue.put_nowait(item)
    except queue.Full:
      # Buang pesan tertua agar telemetry terbaru tetap dapat ditampilkan.
      data_queue.get_nowait()
      data_queue.put_nowait(item)

  def on_message(client, userdata, msg):
    try:
      payload_str = msg.payload.decode("utf-8")
      if msg.topic == TOPIC_DATA:
        data = json.loads(payload_str)
        if not isinstance(data, dict):
          raise ValueError("Payload data harus berupa object JSON")
        enqueue({"topic": msg.topic, "data": data})
      elif msg.topic == TOPIC_STATUS:
        # TOPIC_STATUS mengirim string biasa, bukan JSON.
        enqueue({"topic": msg.topic, "status": payload_str})
    except Exception as ex:
      print("Parse error:", ex)

  def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
      client.subscribe([(TOPIC_DATA, 0), (TOPIC_STATUS, 0)])
      enqueue({"topic": "__connection__", "connected": True})
      print("MQTT connected")
    else:
      print(f"MQTT connection rejected: {reason_code}")

  def on_disconnect(client, userdata, disconnect_flags=None, reason_code=None, properties=None):
    # Paho MQTT 1.x mengirim rc sebagai argumen ketiga, sedangkan Paho 2.x
    # mengirim disconnect_flags lalu reason_code.
    actual_reason = reason_code if reason_code is not None else disconnect_flags
    enqueue({"topic": "__connection__", "connected": False})
    print(f"MQTT disconnected: {actual_reason}")

  # Broker publik akan memutus client lama jika client ID sama. ID unik juga
  # mencegah beberapa tab/dashboard lokal saling menendang.
  client_id = f"smartroom-dashboard-{uuid.uuid4().hex[:12]}"
  try:
    client = mqtt.Client(
      callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
      client_id=client_id,
      protocol=mqtt.MQTTv311,
    )
  except AttributeError:
    # Kompatibel dengan paho-mqtt versi lama yang belum memiliki API version.
    client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)
  client.on_connect = on_connect
  client.on_disconnect = on_disconnect
  client.on_message = on_message
  client.reconnect_delay_set(min_delay=2, max_delay=30)

  while True:
    try:
      client.connect(MQTT_HOST, MQTT_PORT, 60)
      # loop_forever menangani network loop dan reconnect setelah koneksi putus.
      client.loop_forever(retry_first_connection=True)
    except Exception as ex:
      enqueue({"topic": "__connection__", "connected": False})
      print(f"MQTT init/reconnect error: {ex}")
    # loop_forever dapat kembali tanpa exception setelah koneksi ditutup.
    # Beri jeda agar tidak membuat reconnect storm ke broker publik.
    time.sleep(3)


# 3. Jalankan background worker sekali saja saat aplikasi pertama kali dicheck
@st.cache_resource
def init_mqtt_thread():
  t = threading.Thread(target=background_mqtt_worker, daemon=True)
  t.start()
  return True


init_mqtt_thread()

# 4. Inisialisasi data history di session_state Streamlit agar bertahan di layar
if "data_history" not in st.session_state:
  st.session_state.data_history = []
if "current_status" not in st.session_state:
  st.session_state.current_status = "AMAN"
if "mqtt_connected" not in st.session_state:
  st.session_state.mqtt_connected = False

# 5. Ambil data dari queue secara aman dan masukkan ke session_state
while not data_queue.empty():
  item = data_queue.get()
  if item["topic"] == "__connection__":
    st.session_state.mqtt_connected = item["connected"]
  elif item["topic"] == TOPIC_STATUS:
    st.session_state.current_status = item["status"]
  else:
    data = item["data"]
    # Status dari topic khusus menjadi sumber kebenaran terbaru. Jika belum
    # ada pesan status, gunakan status yang ikut dikirim pada telemetry.
    data["status"] = st.session_state.current_status = data.get(
      "status", st.session_state.current_status
    )
    st.session_state.data_history.append(data)
    # Batasi maksimal 30 data terakhir agar grafik tidak terlalu panjang
    if len(st.session_state.data_history) > 30:
      st.session_state.data_history.pop(0)

st.caption(
  "MQTT: terhubung" if st.session_state.mqtt_connected
  else "MQTT: mencoba terhubung..."
)

# --- LAYOUT DASHBOARD ---
if len(st.session_state.data_history) > 0:
  latest = st.session_state.data_history[-1]

  # Kotak Metrik Atas (Card Style)
  col1, col2, col3, col4 = st.columns(4)
  col1.metric("Status Ruangan", st.session_state.current_status)
  col2.metric("Suhu", f"{latest.get('suhu', 0)} °C")
  col3.metric("Kelembapan", f"{latest.get('kelembapan', 0)} %")
  col4.metric("Cahaya (LDR)", latest.get("cahaya", 0))

  st.markdown("---")

  # Grafik Tren Interaktif
  df = pd.DataFrame(st.session_state.data_history)
  st.subheader("📈 Grafik Tren Suhu & Kelembapan Real-time")
  st.line_chart(df, y=["suhu", "kelembapan"])
else:
  st.warning(
      "⏳ Menunggu data masuk dari Wokwi... Pastikan simulasi Wokwi sedang"
      " berjalan (Play) dan terhubung ke broker MQTT."
  )

# Auto-refresh halus setiap 2 detik tanpa merusak socket browser
time.sleep(2)
st.rerun()