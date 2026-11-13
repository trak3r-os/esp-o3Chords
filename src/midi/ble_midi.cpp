#include "midi/ble_midi.h"
#include "engine/midi_clock.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "nvs_flash.h"
#include <cstring>

// NimBLE includes
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "esp_nimble_hci.h"

// Not exposed via a public header in IDF; declare explicitly.
extern "C" void ble_store_config_init(void);

static const char* TAG = "BleMidi";

// MIDI BLE Service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
static const ble_uuid128_t midi_service_uuid =
    BLE_UUID128_INIT(0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                     0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);

// MIDI I/O Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3
static const ble_uuid128_t midi_char_uuid =
    BLE_UUID128_INIT(0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
                     0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77);

// Static pointer to singleton instance for callbacks
static midi::BleMidi* g_ble_midi_instance = nullptr;

namespace midi {

BleMidi::BleMidi(Router& router) : router_(router) {
  router_.register_sink(this);
  g_ble_midi_instance = this;
}

BleMidi::~BleMidi() {
  if (initialized_) {
    nimble_port_stop();
    nimble_port_deinit();
    esp_nimble_hci_deinit();
    initialized_ = false;
    connected_ = false;
  }
  g_ble_midi_instance = nullptr;
}

// GATT characteristic access callback (static C-style for NimBLE)
static int midi_char_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      ESP_LOGI(TAG, "MIDI characteristic read");
      return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
      // Handle incoming MIDI data from host
      if (ctxt->om) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        uint8_t data[256];  // the preferred MTU; larger writes cannot arrive
        if (len <= sizeof(data)) {
          ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL);
          ESP_LOGD(TAG, "Received MIDI data: %d bytes", len);

          // Pick out system real-time bytes (clock/start/continue/stop).
          // A BLE MIDI packet is: header, then repeated [timestamp, status,
          // data...]. Timestamps also have the top bit set and cover the
          // whole 0x80-0xFF range, so a byte is only a status when it
          // directly follows a timestamp; five timestamp values in every 128
          // would otherwise be read as clock or transport messages.
          if (g_ble_midi_instance && g_ble_midi_instance->midi_clock()) {
            bool after_timestamp = false;
            for (uint16_t i = 1; i < len; ++i) {
              const uint8_t b = data[i];
              if (!(b & 0x80)) { after_timestamp = false; continue; }  // data byte
              if (!after_timestamp) { after_timestamp = true; continue; }  // timestamp
              after_timestamp = false;                                     // status
              if (b >= 0xF8 && b <= 0xFC) {
                g_ble_midi_instance->midi_clock()->feed_byte(
                    b, engine::ClockSource::kBle);
              }
            }
          }
        }
      }
      return 0;

    default:
      return BLE_ATT_ERR_UNLIKELY;
  }
}

// GATT service and characteristic definitions
static struct ble_gatt_chr_def gatt_characteristics[] = {
    {
        .uuid = &midi_char_uuid.u,
        .access_cb = midi_char_access_cb,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = nullptr,  // Will be set to midi_char_val_handle_
        .cpfd = nullptr,
    },
    {
        .uuid = nullptr,
        .access_cb = nullptr,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = 0,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },  // Terminator
};

static struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &midi_service_uuid.u,
        .includes = nullptr,
        .characteristics = gatt_characteristics,
    },
    {
        .type = 0,
        .uuid = nullptr,
        .includes = nullptr,
        .characteristics = nullptr,
    },  // Terminator
};

// GAP event callback
int BleMidi::gap_event_callback(struct ble_gap_event* event, void* arg) {
  BleMidi* self = static_cast<BleMidi*>(arg);

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(TAG, "BLE GAP event: Connect, status=%d", event->connect.status);
      if (event->connect.status == 0) {
        self->conn_handle_ = event->connect.conn_handle;
        self->connected_ = true;
        self->subscribed_ = false;
        ESP_LOGI(TAG, "BLE connected, handle=%d", self->conn_handle_);
      } else {
        // Connection failed, restart advertising
        self->start_advertising();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
      self->connected_ = false;
      self->subscribed_ = false;
      self->conn_handle_ = 0;
      self->needs_panic_ = true;
      self->panic_step_ = 0;  // the next link gets the whole recovery, from channel 1
      // Restart advertising if still enabled
      if (self->enabled_) self->start_advertising();
      break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "BLE advertising complete");
      if (self->enabled_) self->start_advertising();
      break;

    case BLE_GAP_EVENT_SUBSCRIBE:
      ESP_LOGI(TAG, "BLE subscribe event, conn_handle=%d attr_handle=%d",
               event->subscribe.conn_handle, event->subscribe.attr_handle);
      // Only the MIDI characteristic's own subscription counts; a central
      // touching another CCCD (GATT Service Changed) must not flip it.
      if (event->subscribe.attr_handle == self->midi_char_val_handle_)
        self->subscribed_ = event->subscribe.cur_notify || event->subscribe.cur_indicate;
      break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      // The central pairs again although we still hold a bond for it (it was
      // "forgotten" on the phone/computer). Drop our stale bond and let the
      // pairing proceed, instead of refusing the connection forever.
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
        ble_store_util_delete_peer(&desc.peer_id_addr);
      ESP_LOGI(TAG, "BLE repeat pairing: old bond dropped");
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
      // Encryption failed with a peer that kept its bond while ours is gone
      // (after a reboot or Reset): forget it so the central can pair afresh.
      if (event->enc_change.status != 0) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
          ble_store_util_delete_peer(&desc.peer_id_addr);
        ESP_LOGW(TAG, "BLE encryption failed, status=%d; bond dropped", event->enc_change.status);
      }
      break;

    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "BLE MTU updated: %d", event->mtu.value);
      break;
  }

  return 0;
}

void BleMidi::start_advertising() {
  // Keep adv payload tiny: flags + service UUID. Put name in scan response.
  struct ble_hs_adv_fields adv_fields;
  memset(&adv_fields, 0, sizeof(adv_fields));
  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  adv_fields.uuids128 = &midi_service_uuid;
  adv_fields.num_uuids128 = 1;
  adv_fields.uuids128_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting advertisement data: %d", rc);
    return;
  }

  struct ble_hs_adv_fields scan_rsp;
  memset(&scan_rsp, 0, sizeof(scan_rsp));
  scan_rsp.name = reinterpret_cast<const uint8_t*>(device_name_);
  scan_rsp.name_len = strlen(device_name_);
  scan_rsp.name_is_complete = 1;

  rc = ble_gap_adv_rsp_set_fields(&scan_rsp);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting scan response data: %d", rc);
    return;
  }

  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(own_addr_type_, NULL, BLE_HS_FOREVER,
                         &adv_params, gap_event_callback, this);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error starting advertising: %d", rc);
  } else {
    ESP_LOGI(TAG, "BLE advertising started");
  }
}

// NimBLE host task
void BleMidi::nimble_host_task(void* param) {
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

// Called when NimBLE stack is synchronized
static void ble_on_sync_callback(void) {
  ESP_LOGI(TAG, "BLE stack synchronized");

  // Ignore missing IRKs on cold start
  if (g_ble_midi_instance) {
    uint8_t addr_type = 0;
    int rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0) {
      ESP_LOGE(TAG, "Failed to infer BLE address type: %d", rc);
      return;
    }
    g_ble_midi_instance->set_own_addr_type(addr_type);
    g_ble_midi_instance->start_advertising();
  }
}

// Called when NimBLE stack is reset
static void ble_on_reset_callback(int reason) {
  ESP_LOGE(TAG, "BLE stack reset, reason=%d", reason);
}

bool BleMidi::begin() {
  if (!enabled_) {
    ESP_LOGI(TAG, "BLE MIDI disabled, skipping initialization");
    return true;
  }

  if (initialized_) {
    ESP_LOGW(TAG, "BLE MIDI already initialized");
    return true;
  }

  ESP_LOGI(TAG, "Initializing BLE MIDI...");

  // Let nimble_port_init() handle controller init/enable + HCI
  esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "BT mem release (classic) failed: %d", ret);
  }

  // Initialize NimBLE host
  int rc = nimble_port_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
    return false;
  }

  // Security / pairing configuration: allow bonding, no MITM, secure connections
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  // Initialize GAP and GATT services
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_store_config_init();

  // Set device name
  rc = ble_svc_gap_device_name_set(device_name_);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting device name: %d", rc);
  }

  // Configure GATT characteristic with instance pointer
  gatt_characteristics[0].arg = this;
  gatt_characteristics[0].val_handle = &midi_char_val_handle_;

  // Register GATT services
  rc = ble_gatts_count_cfg(gatt_services);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error counting GATT config: %d", rc);
    return false;
  }

  rc = ble_gatts_add_svcs(gatt_services);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error adding GATT services: %d", rc);
    return false;
  }

  // Set sync and reset callbacks
  ble_hs_cfg.sync_cb = ble_on_sync_callback;
  ble_hs_cfg.reset_cb = ble_on_reset_callback;

  // Start NimBLE host task
  nimble_port_freertos_init(nimble_host_task);

  initialized_ = true;
  ESP_LOGI(TAG, "BLE MIDI initialized successfully");

  return true;
}

void BleMidi::send_panic() {
  // Resume at the first unsent message; restarting at channel 0 can starve
  // later channels indefinitely when the transport FIFO is small.
  while (panic_step_ < 48) {
    const uint8_t ch = panic_step_ / 3;
    const uint8_t kind = panic_step_ % 3;
    const uint8_t status = (kind == 2 ? 0xE0 : 0xB0) | ch;
    const uint8_t data1 = kind == 0 ? 123 : (kind == 1 ? 120 : 0);
    const uint8_t data2 = kind == 2 ? 64 : 0;
    const uint8_t packet[] = {status, data1, data2};
    if (!send_midi_packet(packet, sizeof(packet))) {
      needs_panic_ = true;
      return;
    }
    ++panic_step_;
  }
  panic_step_ = 0;
}

app::contract::TransportState BleMidi::transport_state() const {
  if (!enabled_) return app::contract::TransportState::Disabled;
  if (!connected_ || !subscribed_) return app::contract::TransportState::Disconnected;
  if (needs_panic_ || panic_step_ != 0) return app::contract::TransportState::Recovering;
  return app::contract::TransportState::Connected;
}

void BleMidi::loop() {
  if (enabled_ && needs_panic_ && connected_ && subscribed_) {
    needs_panic_ = false;
    send_panic();
    // send_panic() re-arms needs_panic_ when the link pushes back; report
    // only a recovery that actually went out, not every retry.
    if (!needs_panic_ && panic_step_ == 0) ESP_LOGI(TAG, "Sent stuck-note recovery after reconnect");
  }
}

void BleMidi::enable(bool enabled) {
  // Settings are re-applied on every parameter edit; an unchanged state must
  // not restart advertising (or log) each time the encoder turns.
  if (enabled == enabled_ && (initialized_ || !enabled)) return;
  if (enabled_ && !enabled) {
    send_panic();  // Send while the transport is still enabled.
    needs_panic_ = true;  // Also recover on re-enable if delivery was interrupted.
  }
  enabled_ = enabled;
  ESP_LOGI(TAG, "BLE MIDI %s", enabled ? "enabled" : "disabled");

  if (!initialized_) {
    // First enable brings the stack up - at boot, or later from the menu
    // when BLE was off at boot, so enabling it never needs a reboot.
    if (enabled_) begin();
    return;
  }

  if (enabled_) {
    // Resume advertising if we were previously disabled.
    start_advertising();
    return;
  }

  // Disable: stop advertising and disconnect any active connection.
  if (connected_) {
    (void)ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
  }

  (void)ble_gap_adv_stop();
  connected_ = false;
  subscribed_ = false;
  conn_handle_ = 0;
}

void BleMidi::set_device_name(const char* name) {
  if (name != nullptr) {
    if (strncmp(device_name_, name, sizeof(device_name_) - 1) == 0) return;  // unchanged
    strncpy(device_name_, name, sizeof(device_name_) - 1);
    device_name_[sizeof(device_name_) - 1] = '\0';
    ESP_LOGI(TAG, "BLE device name set to '%s'", device_name_);

    // Update name if already initialized
    if (initialized_) {
      ble_svc_gap_device_name_set(device_name_);
    }
  }
}

bool BleMidi::send_midi_packet(const uint8_t* data, size_t len) {
  if (!enabled_ || !connected_ || !initialized_) {
    last_result_ = app::contract::SendResult::Disconnected;
    return false;
  }

  // Get current timestamp (13-bit, 0-8191ms, wraps every ~8.2 seconds)
  uint16_t timestamp_ms = (esp_timer_get_time() / 1000) & 0x1FFF;

  // Build BLE MIDI packet with timestamp header
  uint8_t ble_packet[32];
  ble_packet[0] = 0x80 | ((timestamp_ms >> 7) & 0x3F);  // Header: MSB=1, bit6=0, upper 6 bits of timestamp
  ble_packet[1] = 0x80 | (timestamp_ms & 0x7F);         // Timestamp low: MSB=1, lower 7 bits

  // Copy MIDI data
  if (len > sizeof(ble_packet) - 2) {
    len = sizeof(ble_packet) - 2;
  }
  memcpy(&ble_packet[2], data, len);
  size_t total_len = 2 + len;

  // Send notification to connected device
  // Backpressure: skip if not subscribed or buffers low
  if (!subscribed_) {
    last_result_ = app::contract::SendResult::Disconnected;
    return false;
  }

  // Recover later if backpressure could have dropped a note release.
  if (os_msys_num_free() < 4) {
    needs_panic_ = true;
    last_result_ = app::contract::SendResult::Backpressure;
    return false;
  }

  struct os_mbuf* om = ble_hs_mbuf_from_flat(ble_packet, total_len);
  if (!om) {
    needs_panic_ = true;
    last_result_ = app::contract::SendResult::Backpressure;
    return false;
  }

  int rc = ble_gattc_notify_custom(conn_handle_, midi_char_val_handle_, om);
  if (rc != 0) {
    needs_panic_ = true;
    last_result_ = app::contract::SendResult::Backpressure;
    ESP_LOGW(TAG, "Error sending BLE notification: %d", rc);
    return false;
  }
  last_result_ = app::contract::SendResult::Accepted;
  return true;
}

void BleMidi::send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (transport_state() != app::contract::TransportState::Connected) {
    last_result_ = transport_state() == app::contract::TransportState::Recovering
                       ? app::contract::SendResult::Backpressure
                       : app::contract::SendResult::Disconnected;
    return;
  }

  uint8_t midi_data[3] = {
      static_cast<uint8_t>(0x90 | (channel & 0x0F)),
      static_cast<uint8_t>(note & 0x7F),
      static_cast<uint8_t>(velocity & 0x7F)};

  send_midi_packet(midi_data, 3);
}

void BleMidi::send_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (!enabled_ || !connected_) {
    last_result_ = app::contract::SendResult::Disconnected;
    return;
  }

  uint8_t midi_data[3] = {
      static_cast<uint8_t>(0x80 | (channel & 0x0F)),
      static_cast<uint8_t>(note & 0x7F),
      static_cast<uint8_t>(velocity & 0x7F)};

  send_midi_packet(midi_data, 3);
}

void BleMidi::send_pitch_bend(uint8_t channel, int16_t value) {
  if (!enabled_ || !connected_) {
    return;
  }

  // Convert signed value to 14-bit unsigned (0-16383, center=8192)
  uint16_t bend_value = static_cast<uint16_t>(value + 8192) & 0x3FFF;

  uint8_t midi_data[3] = {
      static_cast<uint8_t>(0xE0 | (channel & 0x0F)),
      static_cast<uint8_t>(bend_value & 0x7F),        // LSB
      static_cast<uint8_t>((bend_value >> 7) & 0x7F)  // MSB
  };

  send_midi_packet(midi_data, 3);
}

void BleMidi::send_control_change(uint8_t channel, uint8_t controller, uint8_t value) {
  if (!enabled_ || !connected_) {
    return;
  }

  uint8_t midi_data[3] = {
      static_cast<uint8_t>(0xB0 | (channel & 0x0F)),
      controller,
      value
  };

  send_midi_packet(midi_data, 3);
}

void BleMidi::send_channel_pressure(uint8_t channel, uint8_t value) {
  if (!enabled_ || !connected_) {
    return;
  }
  const uint8_t midi_data[2] = {static_cast<uint8_t>(0xD0 | (channel & 0x0F)),
                                static_cast<uint8_t>(value & 0x7F)};
  send_midi_packet(midi_data, 2);
}

}  // namespace midi
