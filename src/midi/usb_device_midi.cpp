#include "midi/usb_device_midi.h"
#include "engine/midi_clock.h"
#include "esp_log.h"

#if __has_include("esp_mac.h")
#include "esp_mac.h"
#define HAVE_ESP_MAC 1
#else
#define HAVE_ESP_MAC 0
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if __has_include("tinyusb.h") && __has_include("class/midi/midi_device.h")
#define HAVE_TINYUSB 1
#include "tinyusb.h"
#include "class/midi/midi_device.h"
#else
#define HAVE_TINYUSB 0
#endif

static const char* TAG = "UsbDeviceMidi";

// USB device descriptor (ESP VID, custom PID)
static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,      // Espressif VID
    .idProduct = 0x4002,     // Custom PID for MIDI
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01};

// Interface indices
enum {
  ITF_NUM_MIDI = 0,
  ITF_NUM_MIDI_STREAMING,
  ITF_COUNT
};

// Endpoint numbers (EP IN has MSB set)
enum {
  EPNUM_MIDI_OUT = 0x01,
  EPNUM_MIDI_IN = 0x81
};

#define TUSB_DESCRIPTOR_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const char kLangDescriptor[] = {0x09, 0x04};  // English

// Serial number is filled from the WiFi/BT MAC address at begin() time.
// Format: 12 hex chars (e.g. "AABBCCDDEEFF") + NUL.
static char g_serial_str[13] = "000000000000";

static const char* string_desc[] = {
    kLangDescriptor,
    "Espressif",        // Manufacturer
    "o3Chords",         // Product
    g_serial_str,       // Serial number — populated from MAC in begin()
    "o3Chords MIDI",    // MIDI interface name
};

static const uint8_t midi_cfg_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power (mA)
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),
    // Interface number, string index, EP Out & In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

namespace midi {

UsbDeviceMidi::UsbDeviceMidi(Router& router) : router_(router) {
  router_.register_sink(this);
}

bool UsbDeviceMidi::begin() {
  if (!enabled_) {
    ESP_LOGI(TAG, "USB Device MIDI disabled, skipping initialization");
    return true;
  }

#if !HAVE_TINYUSB
  ESP_LOGW(TAG, "TinyUSB component not found in this build; USB Device MIDI disabled");
  initialized_ = false;
  connected_ = false;
  return false;
#endif

  if (initialized_) {
    ESP_LOGW(TAG, "USB Device MIDI already initialized");
    return true;
  }

  ESP_LOGI(TAG, "Initializing USB Device MIDI...");

  // Populate serial number from the chip's default MAC address so each
  // device presents a unique USB serial number instead of "123456".
#if HAVE_ESP_MAC
  {
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
      snprintf(g_serial_str, sizeof(g_serial_str),
               "%02X%02X%02X%02X%02X%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      ESP_LOGI(TAG, "USB serial: %s", g_serial_str);
    }
  }
#endif

  tinyusb_config_t tusb_cfg = {
      .port = TINYUSB_PORT_FULL_SPEED_0,
      .phy = {
          .skip_setup = false,
          .self_powered = false,
          .vbus_monitor_io = -1,
      },
      .task = {
          .size = 4096,
          .priority = 5,
          .xCoreID = 0,  // Pin TinyUSB task to core 0 to satisfy IDF check
      },
      .descriptor = {
          .device = &device_descriptor,
          .qualifier = nullptr,
          .string = string_desc,
          .string_count = static_cast<int>(sizeof(string_desc) / sizeof(string_desc[0])),
          .full_speed_config = midi_cfg_descriptor,
          .high_speed_config = nullptr,
      },
      .event_cb = nullptr,
      .event_arg = nullptr,
  };

  esp_err_t err = tinyusb_driver_install(&tusb_cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "TinyUSB already installed, continuing");
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(err));
    return false;
  }

  initialized_ = true;
  connected_ = tud_midi_mounted();
  ESP_LOGI(TAG, "USB Device MIDI initialized");

  return true;
}

void UsbDeviceMidi::loop() {
#if !HAVE_TINYUSB
  (void)enabled_;
  return;
#else
  if (!enabled_ || !initialized_) {
    return;
  }

  bool mounted = tud_midi_mounted();
  if (mounted != connected_) {
    if (!mounted) { needs_panic_ = true; panic_step_ = 0; }
    connected_ = mounted;
    ESP_LOGI(TAG, "USB Device MIDI %s", mounted ? "connected" : "disconnected");
  }

  if (needs_panic_ && connected_) {
    needs_panic_ = false;
    send_panic();
    // send_panic() re-arms needs_panic_ while the host is not draining the
    // endpoint; report only a recovery that actually went out.
    if (!needs_panic_ && panic_step_ == 0) ESP_LOGI(TAG, "Sent stuck-note recovery after USB reconnect");
  }

  uint8_t packet[4];
  while (tud_midi_available()) {
    if (tud_midi_packet_read(packet)) {
      ESP_LOGD(TAG, "Received MIDI: CIN=%02x Status=%02x Data1=%02x Data2=%02x",
               packet[0] & 0x0F, packet[1], packet[2], packet[3]);

      // Forward system realtime clock bytes to MidiClock.
      const uint8_t status = packet[1];
      if (midi_clock_ && status >= 0xF8 && status <= 0xFC) {
        midi_clock_->feed_byte(status, engine::ClockSource::kUsb);
      }
    } else {
      break;
    }
  }
#endif
}

app::contract::TransportState UsbDeviceMidi::transport_state() const {
  if (!enabled_) return app::contract::TransportState::Disabled;
  if (!connected_) return app::contract::TransportState::Disconnected;
  if (needs_panic_ || panic_step_ != 0) return app::contract::TransportState::Recovering;
  return app::contract::TransportState::Connected;
}

void UsbDeviceMidi::enable(bool enabled) {
  if (enabled_ && !enabled) {
    send_panic();  // Send while the transport is still enabled.
    needs_panic_ = true;  // Also recover on re-enable if delivery was interrupted.
  }
  enabled_ = enabled;
  ESP_LOGI(TAG, "USB Device MIDI %s", enabled ? "enabled" : "disabled");
}

void UsbDeviceMidi::send_panic() {
  // Resume at the first unsent message; restarting at channel 0 can starve
  // later channels indefinitely when the transport FIFO is small.
  while (panic_step_ < 48) {
    const uint8_t ch = panic_step_ / 3;
    const uint8_t kind = panic_step_ % 3;
    const uint8_t status = (kind == 2 ? 0xE0 : 0xB0) | ch;
    const uint8_t data1 = kind == 0 ? 123 : (kind == 1 ? 120 : 0);
    const uint8_t data2 = kind == 2 ? 64 : 0;
    if (!send_usb_midi_packet(0, status, data1, data2)) {
      needs_panic_ = true;
      return;
    }
    ++panic_step_;
  }
  panic_step_ = 0;
}

bool UsbDeviceMidi::send_usb_midi_packet(uint8_t cable_num, uint8_t status, uint8_t data1, uint8_t data2) {
#if !HAVE_TINYUSB
  (void)cable_num;
  (void)status;
  (void)data1;
  (void)data2;
  last_result_ = app::contract::SendResult::Disconnected;
  return false;
#else
  if (!enabled_ || !initialized_ || !tud_midi_mounted()) {
    // A send can notice the unplug before loop() does; the notes that were
    // sounding still need the recovery when the cable comes back.
    if (connected_) { needs_panic_ = true; panic_step_ = 0; }
    connected_ = false;
    last_result_ = app::contract::SendResult::Disconnected;
    return false;
  }

  uint8_t packet[4] = {
      static_cast<uint8_t>((cable_num << 4) | (status >> 4)),
      static_cast<uint8_t>(status),
      static_cast<uint8_t>(data1),
      static_cast<uint8_t>(data2),
  };

  // Packet writes are atomic; retrying a partial stream could corrupt MIDI.
  if (!tud_midi_packet_write(packet)) {
    needs_panic_ = true;
    last_result_ = app::contract::SendResult::Backpressure;
    return false;
  }
  last_result_ = app::contract::SendResult::Accepted;
  return true;
#endif
}

void UsbDeviceMidi::send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (transport_state() == app::contract::TransportState::Recovering) {
    last_result_ = app::contract::SendResult::Backpressure;
    return;
  }
  send_usb_midi_packet(0,
                       static_cast<uint8_t>(0x90 | (channel & 0x0F)),
                       static_cast<uint8_t>(note & 0x7F),
                       static_cast<uint8_t>(velocity & 0x7F));
}

void UsbDeviceMidi::send_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
  send_usb_midi_packet(0,
                       static_cast<uint8_t>(0x80 | (channel & 0x0F)),
                       static_cast<uint8_t>(note & 0x7F),
                       static_cast<uint8_t>(velocity & 0x7F));
}

void UsbDeviceMidi::send_pitch_bend(uint8_t channel, int16_t value) {
  // Convert signed value to 14-bit unsigned (0-16383, center=8192)
  uint16_t bend_value = static_cast<uint16_t>(value + 8192) & 0x3FFF;

  send_usb_midi_packet(0,
                       static_cast<uint8_t>(0xE0 | (channel & 0x0F)),
                       static_cast<uint8_t>(bend_value & 0x7F),
                       static_cast<uint8_t>((bend_value >> 7) & 0x7F));
}

void UsbDeviceMidi::send_control_change(uint8_t channel, uint8_t controller, uint8_t value) {
  send_usb_midi_packet(0,
                       static_cast<uint8_t>(0xB0 | (channel & 0x0F)),
                       controller,
                       value);
}

void UsbDeviceMidi::send_channel_pressure(uint8_t channel, uint8_t value) {
  // CIN 0xD: a two-byte message, third data byte unused.
  send_usb_midi_packet(0, static_cast<uint8_t>(0xD0 | (channel & 0x0F)), value & 0x7F, 0);
}

}  // namespace midi
