#include "midi/usb_host_midi.h"
#include "engine/midi_clock.h"
#include "esp_log.h"

#if __has_include("usb/usb_host.h") && __has_include("freertos/FreeRTOS.h")
#define HAVE_USB_HOST 1
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#else
#define HAVE_USB_HOST 0
#endif

static const char* TAG = "UsbHostMidi";

// USB Audio Class 1.0 identifiers for MIDI Streaming interface
static constexpr uint8_t kAudioClass   = 0x01;
static constexpr uint8_t kMidiSubclass = 0x03;

// USB MIDI Event Packet is always 4 bytes (USB Audio Class 1.0 §4)
static constexpr size_t kMidiPktSize = 4;

// Bulk endpoint transfer buffer (USB Full-Speed max bulk packet = 64 bytes)
static constexpr size_t kBulkBufSize = 64;

namespace midi {

#if HAVE_USB_HOST

// Singleton — needed for C-style transfer and event callbacks.
static UsbHostMidi* g_usb_host = nullptr;

// Outbound queue. There is one bulk-out transfer, and it stays in flight for
// milliseconds, so the notes of a chord (sent back to back) have to wait their
// turn: they are queued here and go out up to 16 to a transfer. Senders are
// the main loop and the esp_timer task; completion and unplug arrive on the
// USB client task - g_out_mutex guards the queue, the busy flag and the
// transfer pointer for all three.
static constexpr size_t kOutQueuePackets = 64;
static SemaphoreHandle_t g_out_mutex = nullptr;
static uint8_t g_out_queue[kOutQueuePackets][kMidiPktSize];
static size_t g_out_head = 0;
static size_t g_out_count = 0;
static bool g_send_busy = false;

struct UsbHostCtx {
  usb_host_client_handle_t client_hdl   = nullptr;
  usb_device_handle_t      dev_hdl      = nullptr;
  usb_transfer_t*          bulk_in      = nullptr;
  usb_transfer_t*          bulk_out     = nullptr;
  TaskHandle_t             host_task    = nullptr;
  TaskHandle_t             client_task_h = nullptr;

  uint8_t dev_addr        = 0;
  uint8_t intf_num        = 0xFF;
  uint8_t ep_in           = 0;
  uint8_t ep_out          = 0;
  bool    device_claimed  = false;
  // Set by bulk_in_cb when reception failed; the client task loop clears the
  // endpoint and restarts reception (not legal from inside the callback).
  bool    in_needs_recovery = false;
  uint8_t in_error_streak   = 0;
};

// A device that keeps failing is left alone until it is replugged.
static constexpr uint8_t kMaxInRecoveries = 5;

// -------------------------------------------------------------------------
// Descriptor helpers

// Scan a config descriptor for a USB MIDI Streaming interface and its bulk
// endpoints.  Returns true if at least one bulk-in endpoint was found.
static bool find_midi_interface(const usb_config_desc_t* cfg,
                                 uint8_t& intf_out,
                                 uint8_t& ep_in_out,
                                 uint8_t& ep_out_out) {
  int  off      = 0;
  bool in_midi  = false;
  bool found_in = false;
  ep_out_out = 0;

  while (off < cfg->wTotalLength) {
    const auto* d = reinterpret_cast<const usb_standard_desc_t*>(
        reinterpret_cast<const uint8_t*>(cfg) + off);
    if (!d->bLength) break;

    if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const auto* id = reinterpret_cast<const usb_intf_desc_t*>(d);
      in_midi = (id->bInterfaceClass    == kAudioClass &&
                 id->bInterfaceSubClass == kMidiSubclass);
      if (in_midi) intf_out = id->bInterfaceNumber;
    }

    if (in_midi && d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const auto* ed = reinterpret_cast<const usb_ep_desc_t*>(d);
      if ((ed->bmAttributes & USB_BM_ATTRIBUTES_XFER_BULK) != 0) {
        if (ed->bEndpointAddress & 0x80) {
          ep_in_out = ed->bEndpointAddress;
          found_in  = true;
        } else {
          ep_out_out = ed->bEndpointAddress;
        }
      }
    }
    off += d->bLength;
  }
  return found_in;
}

// -------------------------------------------------------------------------
// MIDI packet dispatcher

// Parse a single 4-byte USB MIDI Event Packet.
// Realtime clock bytes (0xF8-0xFC) → MidiClock::feed_byte.
// Channel messages → Router fan-out.
static void dispatch_packet(UsbHostMidi* self, const uint8_t* pkt) {
  // pkt[0] = Cable[4b] | CIN[4b]   pkt[1-3] = MIDI status + data
  const uint8_t status = pkt[1];
  const uint8_t d1     = pkt[2];
  const uint8_t d2     = pkt[3];

  if (status >= 0xF8 && status <= 0xFC) {
    if (self->midi_clock())
      self->midi_clock()->feed_byte(status, engine::ClockSource::kUsb);
    return;
  }

  const uint8_t type = status >> 4;
  const uint8_t ch   = status & 0x0F;
  switch (type) {
    case 0x9:
      if (d2 > 0) self->router_ref().forward_note_on(self, ch, d1, d2);
      else        self->router_ref().forward_note_off(self, ch, d1, 0);
      break;
    case 0x8:
      self->router_ref().forward_note_off(self, ch, d1, d2);
      break;
    case 0xB:
      self->router_ref().forward_control_change(self, ch, d1, d2);
      break;
    case 0xE: {
      int16_t bend = static_cast<int16_t>((d2 << 7) | d1) - 8192;
      self->router_ref().forward_pitch_bend(self, ch, bend);
      break;
    }
    default: break;
  }
}

// -------------------------------------------------------------------------
// Transfer callbacks (called from USB host task context)

static void bulk_in_cb(usb_transfer_t* xfer) {
  if (!g_usb_host) return;

  if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0) {
    const uint8_t* p = xfer->data_buffer;
    for (size_t rem = static_cast<size_t>(xfer->actual_num_bytes);
         rem >= kMidiPktSize;
         rem -= kMidiPktSize, p += kMidiPktSize) {
      dispatch_packet(g_usb_host, p);
    }
  }

  auto* ctx = static_cast<UsbHostCtx*>(xfer->context);
  switch (xfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED:
      if (ctx) ctx->in_error_streak = 0;
      xfer->num_bytes = kBulkBufSize;
      usb_host_transfer_submit(xfer);
      break;
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
      // Unplug or teardown. DEV_GONE is delivered after outstanding transfer
      // callbacks; releasing or closing anything from here is invalid in
      // ESP-IDF and can assert the USB client task.
      break;
    default:
      // Bus error, stall, overflow: the endpoint is halted and a plain
      // re-submit would be refused, leaving input dead until replug. The
      // client task loop clears it and restarts reception.
      if (ctx && ctx->client_hdl) {
        ctx->in_needs_recovery = true;
        usb_host_client_unblock(ctx->client_hdl);
      }
      break;
  }
}

// Runs on the client task, outside any callback.
static void recover_bulk_in(UsbHostCtx* ctx) {
  ctx->in_needs_recovery = false;
  if (!ctx->dev_hdl || !ctx->bulk_in || !ctx->device_claimed) return;
  if (++ctx->in_error_streak > kMaxInRecoveries) {
    ESP_LOGE(TAG, "MIDI input keeps failing; giving up until the device is replugged");
    return;
  }
  ESP_LOGW(TAG, "MIDI input error; restarting reception (%u/%u)",
           ctx->in_error_streak, kMaxInRecoveries);
  usb_host_endpoint_halt(ctx->dev_hdl, ctx->ep_in);   // already halted after an error: result ignored
  usb_host_endpoint_flush(ctx->dev_hdl, ctx->ep_in);
  usb_host_endpoint_clear(ctx->dev_hdl, ctx->ep_in);
  ctx->bulk_in->num_bytes = kBulkBufSize;
  if (usb_host_transfer_submit(ctx->bulk_in) != ESP_OK)
    ESP_LOGE(TAG, "Could not restart MIDI input");
}

static void submit_pending_locked(UsbHostCtx* ctx);

static void bulk_out_cb(usb_transfer_t* xfer) {
  if (!g_out_mutex) return;
  xSemaphoreTake(g_out_mutex, portMAX_DELAY);
  g_send_busy = false;
  submit_pending_locked(static_cast<UsbHostCtx*>(xfer->context));
  xSemaphoreGive(g_out_mutex);
}

// Moves as many queued packets as fit into the idle transfer and submits it.
// Caller holds g_out_mutex.
static void submit_pending_locked(UsbHostCtx* ctx) {
  if (g_send_busy || g_out_count == 0 || !ctx || !ctx->bulk_out) return;
  const size_t n = g_out_count < kBulkBufSize / kMidiPktSize ? g_out_count : kBulkBufSize / kMidiPktSize;
  uint8_t* buf = ctx->bulk_out->data_buffer;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* pkt = g_out_queue[(g_out_head + i) % kOutQueuePackets];
    for (size_t b = 0; b < kMidiPktSize; ++b) buf[i * kMidiPktSize + b] = pkt[b];
  }
  ctx->bulk_out->num_bytes = static_cast<int>(n * kMidiPktSize);
  g_send_busy = true;
  if (usb_host_transfer_submit(ctx->bulk_out) != ESP_OK) {
    g_send_busy = false;
    g_out_count = 0;  // the pipe is not taking data; do not replay stale notes later
    return;
  }
  g_out_head = (g_out_head + n) % kOutQueuePackets;
  g_out_count -= n;
}

// -------------------------------------------------------------------------
// Client event callback (called from within usb_host_client_handle_events)

static void client_event_cb(const usb_host_client_event_msg_t* msg, void* arg) {
  UsbHostCtx* ctx = static_cast<UsbHostCtx*>(arg);
  if (!ctx || !g_usb_host) return;

  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    ctx->dev_addr = msg->new_dev.address;
    ESP_LOGI(TAG, "USB device connected, addr=%u", ctx->dev_addr);

    if (usb_host_device_open(ctx->client_hdl, ctx->dev_addr, &ctx->dev_hdl) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to open USB device");
      ctx->dev_hdl = nullptr;
      return;
    }

    const usb_config_desc_t* cfg = nullptr;
    if (usb_host_get_active_config_descriptor(ctx->dev_hdl, &cfg) != ESP_OK || !cfg) {
      ESP_LOGE(TAG, "Failed to get config descriptor");
      usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
      ctx->dev_hdl = nullptr;
      return;
    }

    uint8_t intf_num = 0xFF, ep_in = 0, ep_out = 0;
    if (!find_midi_interface(cfg, intf_num, ep_in, ep_out)) {
      ESP_LOGW(TAG, "Device addr=%u is not a MIDI device — ignoring", ctx->dev_addr);
      usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
      ctx->dev_hdl = nullptr;
      return;
    }
    ctx->intf_num = intf_num;
    ctx->ep_in    = ep_in;
    ctx->ep_out   = ep_out;

    if (usb_host_interface_claim(ctx->client_hdl, ctx->dev_hdl,
                                  ctx->intf_num, 0) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to claim MIDI interface %u", intf_num);
      usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
      ctx->dev_hdl = nullptr;
      return;
    }
    ctx->device_claimed = true;
    ctx->in_needs_recovery = false;
    ctx->in_error_streak = 0;

    // Allocate and start bulk-in transfer for receiving MIDI data.
    if (usb_host_transfer_alloc(kBulkBufSize, 0, &ctx->bulk_in) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to allocate bulk-in transfer");
      goto cleanup_interface;
    }
    ctx->bulk_in->device_handle    = ctx->dev_hdl;
    ctx->bulk_in->bEndpointAddress = ctx->ep_in;
    ctx->bulk_in->callback         = bulk_in_cb;
    ctx->bulk_in->context          = ctx;
    ctx->bulk_in->num_bytes        = kBulkBufSize;
    ctx->bulk_in->timeout_ms       = 0;  // no timeout — continuous reception

    if (usb_host_transfer_submit(ctx->bulk_in) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to submit bulk-in transfer");
      usb_host_transfer_free(ctx->bulk_in);
      ctx->bulk_in = nullptr;
      goto cleanup_interface;
    }

    // Allocate bulk-out transfer for sending MIDI to the device (optional).
    if (ctx->ep_out != 0 &&
        usb_host_transfer_alloc(kBulkBufSize, 0, &ctx->bulk_out) == ESP_OK) {
      ctx->bulk_out->device_handle    = ctx->dev_hdl;
      ctx->bulk_out->bEndpointAddress = ctx->ep_out;
      ctx->bulk_out->callback         = bulk_out_cb;
      ctx->bulk_out->context          = ctx;
      ctx->bulk_out->timeout_ms       = 50;
    }

    g_usb_host->on_device_ready();
    ESP_LOGI(TAG, "MIDI device ready — intf=%u ep_in=0x%02x ep_out=0x%02x",
             intf_num, ep_in, ep_out);
    return;

  cleanup_interface:
    usb_host_interface_release(ctx->client_hdl, ctx->dev_hdl, ctx->intf_num);
    ctx->device_claimed = false;
    usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
    ctx->dev_hdl = nullptr;

  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    ESP_LOGI(TAG, "USB device disconnected");
    g_usb_host->on_device_gone();
  }
}

// -------------------------------------------------------------------------
// Task entry points

void UsbHostMidi::host_lib_task(void* /*arg*/) {
  ESP_LOGI(TAG, "USB host lib task started");
  while (true) {
    uint32_t flags = 0;
    esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (err == ESP_OK && (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) {
      // All clients deregistered — safe to uninstall the host library.
      usb_host_uninstall();
      break;
    }
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  vTaskDelete(nullptr);
}

void UsbHostMidi::client_task(void* arg) {
  UsbHostCtx* ctx = static_cast<UsbHostCtx*>(arg);
  ESP_LOGI(TAG, "USB MIDI client task started");

  usb_host_client_config_t cfg = {};
  cfg.is_synchronous              = false;
  cfg.max_num_event_msg           = 5;
  cfg.async.client_event_callback = client_event_cb;
  cfg.async.callback_arg          = ctx;

  if (usb_host_client_register(&cfg, &ctx->client_hdl) != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_client_register failed");
    vTaskDelete(nullptr);
    return;
  }

  while (true) {
    // Blocking wait — event callback is dispatched from inside this call.
    esp_err_t err = usb_host_client_handle_events(ctx->client_hdl, portMAX_DELAY);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT && err != ESP_ERR_NOT_FINISHED) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (ctx->in_needs_recovery) recover_bulk_in(ctx);
  }
}

#else  // !HAVE_USB_HOST

// Stub definition so the linker doesn't need an ifdef at every call site.
struct UsbHostCtx {};

#endif  // HAVE_USB_HOST

// ---------------------------------------------------------------------------
// UsbHostMidi — public API
// ---------------------------------------------------------------------------

UsbHostMidi::UsbHostMidi(Router& router) : router_(router) {
  router_.register_sink(this);
}

UsbHostMidi::~UsbHostMidi() {
#if HAVE_USB_HOST
  if (g_usb_host == this) g_usb_host = nullptr;

  if (!ctx_) return;

  // Release any claimed interface and close device handle.
  if (ctx_->device_claimed && ctx_->client_hdl && ctx_->dev_hdl) {
    usb_host_interface_release(ctx_->client_hdl, ctx_->dev_hdl, ctx_->intf_num);
    ctx_->device_claimed = false;
  }
  if (ctx_->bulk_in) {
    usb_host_transfer_free(ctx_->bulk_in);
    ctx_->bulk_in = nullptr;
  }
  if (ctx_->bulk_out) {
    usb_host_transfer_free(ctx_->bulk_out);
    ctx_->bulk_out = nullptr;
  }
  if (ctx_->dev_hdl && ctx_->client_hdl) {
    usb_host_device_close(ctx_->client_hdl, ctx_->dev_hdl);
    ctx_->dev_hdl = nullptr;
  }
  if (ctx_->client_hdl) {
    usb_host_client_deregister(ctx_->client_hdl);
    ctx_->client_hdl = nullptr;
  }
  delete ctx_;
  ctx_ = nullptr;
#endif
}

bool UsbHostMidi::begin() {
  if (!enabled_) {
    ESP_LOGI(TAG, "USB Host MIDI disabled, skipping init");
    return true;
  }
  if (initialized_) {
    ESP_LOGW(TAG, "USB Host MIDI already initialized");
    return true;
  }

#if !HAVE_USB_HOST
  ESP_LOGW(TAG, "usb/usb_host.h not available; USB Host MIDI cannot initialize");
  return false;
#else
  ESP_LOGI(TAG, "Initializing USB Host MIDI...");

  ctx_ = new UsbHostCtx();
  if (!g_out_mutex) g_out_mutex = xSemaphoreCreateMutex();

  g_usb_host = this;

  // Install the USB host library.
  // skip_phy_setup=false: the IDF configures the OTG PHY for host mode.
  usb_host_config_t host_cfg = {};
  host_cfg.skip_phy_setup    = false;
  host_cfg.intr_flags        = ESP_INTR_FLAG_LEVEL1;

  esp_err_t err = usb_host_install(&host_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_install failed: %d", err);
    delete ctx_; ctx_ = nullptr;
    g_usb_host = nullptr;
    return false;
  }

  // Spawn host-library daemon (must run at higher priority than client task).
  BaseType_t ret = xTaskCreatePinnedToCore(
      host_lib_task, "usb_host_lib", 4096, nullptr, 4,
      &ctx_->host_task, 0);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create usb_host_lib task");
    usb_host_uninstall();
    delete ctx_; ctx_ = nullptr;
    g_usb_host = nullptr;
    return false;
  }

  // Spawn MIDI client task.
  ret = xTaskCreatePinnedToCore(
      client_task, "usb_midi_cli", 4096, ctx_, 3,
      &ctx_->client_task_h, 0);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create usb_midi_cli task");
    // host_lib_task will self-delete once usb_host_uninstall() runs.
    usb_host_uninstall();
    delete ctx_; ctx_ = nullptr;
    g_usb_host = nullptr;
    return false;
  }

  initialized_ = true;
  ESP_LOGI(TAG, "USB Host MIDI initialized — waiting for MIDI device");
  return true;
#endif
}

void UsbHostMidi::loop() {
  // All event processing happens in the USB tasks.
  // Nothing to poll from the main loop.
}

void UsbHostMidi::enable(bool enabled) {
  enabled_ = enabled;
  ESP_LOGI(TAG, "USB Host MIDI %s", enabled ? "enabled" : "disabled");
}

void UsbHostMidi::on_device_ready() {
  connected_ = true;
}

void UsbHostMidi::on_device_gone() {
#if HAVE_USB_HOST
  connected_   = false;
  // Senders check connected_/bulk_out under this lock, so the transfer cannot
  // be freed between their check and their use of it.
  if (g_out_mutex) xSemaphoreTake(g_out_mutex, portMAX_DELAY);
  g_send_busy  = false;
  g_out_count  = 0;

  if (!ctx_) { if (g_out_mutex) xSemaphoreGive(g_out_mutex); return; }

  if (ctx_->device_claimed && ctx_->client_hdl && ctx_->dev_hdl) {
    usb_host_interface_release(ctx_->client_hdl, ctx_->dev_hdl, ctx_->intf_num);
    ctx_->device_claimed = false;
  }
  // DEV_GONE is dispatched after transfer completion callbacks, so neither
  // transfer is in flight here and both can be safely released.
  if (ctx_->bulk_in) {
    usb_host_transfer_free(ctx_->bulk_in);
    ctx_->bulk_in = nullptr;
  }
  if (ctx_->bulk_out) {
    usb_host_transfer_free(ctx_->bulk_out);
    ctx_->bulk_out = nullptr;
  }
  if (ctx_->dev_hdl && ctx_->client_hdl) {
    usb_host_device_close(ctx_->client_hdl, ctx_->dev_hdl);
    ctx_->dev_hdl = nullptr;
  }
  if (g_out_mutex) xSemaphoreGive(g_out_mutex);
#endif
}

// ---------------------------------------------------------------------------
// Send helpers

void UsbHostMidi::send_usb_midi(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2) {
#if HAVE_USB_HOST
  if (!enabled_ || !g_out_mutex) return;
  xSemaphoreTake(g_out_mutex, portMAX_DELAY);
  if (connected_ && ctx_ && ctx_->bulk_out && ctx_->ep_out != 0 && g_out_count < kOutQueuePackets) {
    uint8_t* pkt = g_out_queue[(g_out_head + g_out_count) % kOutQueuePackets];
    pkt[0] = cin; pkt[1] = status; pkt[2] = d1; pkt[3] = d2;
    ++g_out_count;
    submit_pending_locked(ctx_);
  }
  xSemaphoreGive(g_out_mutex);
#else
  (void)cin; (void)status; (void)d1; (void)d2;
#endif
}

void UsbHostMidi::send_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
  send_usb_midi(0x09,
                static_cast<uint8_t>(0x90 | (ch & 0x0F)),
                note & 0x7F,
                vel  & 0x7F);
}

void UsbHostMidi::send_note_off(uint8_t ch, uint8_t note, uint8_t vel) {
  send_usb_midi(0x08,
                static_cast<uint8_t>(0x80 | (ch & 0x0F)),
                note & 0x7F,
                vel  & 0x7F);
}

void UsbHostMidi::send_pitch_bend(uint8_t ch, int16_t value) {
  const uint16_t v = static_cast<uint16_t>(value + 8192) & 0x3FFF;
  send_usb_midi(0x0E,
                static_cast<uint8_t>(0xE0 | (ch & 0x0F)),
                static_cast<uint8_t>(v & 0x7F),
                static_cast<uint8_t>((v >> 7) & 0x7F));
}

void UsbHostMidi::send_control_change(uint8_t ch, uint8_t cc, uint8_t val) {
  send_usb_midi(0x0B,
                static_cast<uint8_t>(0xB0 | (ch & 0x0F)),
                cc,
                val);
}

void UsbHostMidi::send_channel_pressure(uint8_t ch, uint8_t val) {
  send_usb_midi(0x0D, static_cast<uint8_t>(0xD0 | (ch & 0x0F)), val & 0x7F, 0);
}

}  // namespace midi
