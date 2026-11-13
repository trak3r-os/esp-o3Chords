#include "app/state.h"
#include "app/types.h"
#include "app/joystick_state.h"
#include "app/ui_state.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/production_ui.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "app/config_codec.h"
#include "config/pins.h"
#include "drivers/i2c_bus.h"
#include "drivers/gpio_buttons.h"
#include "drivers/keypad_matrix.h"
#include "drivers/oled_ssd13xx.h"
#include "drivers/joystick_analog.h"
#include "drivers/rotary_encoder.h"
#include "drivers/storage.h"
#include "engine/engine.h"
#include "engine/note_ledger.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "midi/usb_host_midi.h"
#include "ui/production_render.h"
#include "app/version.h"

#include <cstdio>
#include <vector>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"

using drivers::I2cBus;
using drivers::I2cBusConfig;
using drivers::GpioButtonEvent;
using drivers::GpioButtonEventType;
using drivers::GpioButtonId;
using drivers::GpioButtons;
using drivers::GpioButtonsConfig;
using drivers::KeypadMatrix;
using drivers::KeypadMatrixConfig;
using drivers::KeypadEvent;
using drivers::OledSsd13xx;
using drivers::OledConfig;
using drivers::JoystickAnalog;
using drivers::JoystickAnalogConfig;
using drivers::RotaryEncoder;
using drivers::RotaryEncoderConfig;
using drivers::RotaryEvent;
using drivers::RotaryEventType;
using drivers::Storage;

using midi::Router;
using midi::BleMidi;
using midi::UsbDeviceMidi;
using midi::UsbHostMidi;

using app::JoystickState;
using app::UiNavState;
using app::InputContext;
using app::process_joystick;
using app::service_joystick_ticks;
using app::apply_progression_mode;
using app::apply_settings;
using app::apply_engine_settings;

using ui::ProductionRenderCtx;
using ui::render_production_screen;

static uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
static constexpr uint32_t kOledRetryMs = 5000;

class RouterLedgerSink final : public engine::NoteLedgerSink {
 public:
  explicit RouterLedgerSink(Router& router) : router_(router) {}
  void note_on(uint8_t route, uint8_t channel, uint8_t note, uint8_t velocity) override {
    router_.route_note_on(static_cast<uint8_t>(1u << route), channel, note, velocity);
  }
  void note_off(uint8_t route, uint8_t channel, uint8_t note) override {
    router_.route_note_off(static_cast<uint8_t>(1u << route), channel, note, 0);
  }
 private:
  Router& router_;
};

static bool draw_oled_boot(OledSsd13xx& oled) {
  return ui::render_boot_screen(oled);
}

static bool begin_oled_with_fallback(OledSsd13xx& oled, OledConfig& cfg, bool show_boot = true) {
  const uint8_t primary = cfg.addr;
  const uint8_t alt = primary == 0x3C ? 0x3D : 0x3C;
  const uint8_t addrs[2] = {primary, alt};

  for (uint8_t addr : addrs) {
    cfg.addr = addr;
    oled.reset();
    if (!oled.begin(cfg)) continue;
    if (!show_boot || draw_oled_boot(oled)) return true;
    oled.reset();
  }

  cfg.addr = primary;
  return false;
}

// Reboot into ESP32-S3 USB download mode (DFU).
// Shows a message on the OLED if available, then sets the ROM bootloader
// flag and restarts.  Never returns.
[[noreturn]] static void reboot_to_dfu(OledSsd13xx& oled, bool oled_ok) {
  static const char* TAG = "DFU";
  ESP_LOGW(TAG, "Rebooting into USB download mode...");
  if (oled_ok) {
    oled.clear();
    oled.draw_text_line(0, "o3CHORDS");
    oled.draw_text_line(1, "DFU MODE");
    oled.draw_text_line(2, "RELEASING...");
    oled.flush();
  }
  vTaskDelay(pdMS_TO_TICKS(600));
  REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
  while (true) {}  // unreachable — silences [[noreturn]] warning
}

static void recover_i2c_lines(gpio_num_t sda, gpio_num_t scl) {
  gpio_config_t conf = {};
  conf.intr_type = GPIO_INTR_DISABLE;
  conf.mode = GPIO_MODE_OUTPUT_OD;
  conf.pin_bit_mask = (1ULL << sda) | (1ULL << scl);
  conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&conf);

  gpio_set_level(sda, 1);
  gpio_set_level(scl, 1);
  esp_rom_delay_us(10);
  for (int i = 0; i < 9; ++i) {
    gpio_set_level(scl, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
  }

  gpio_set_level(sda, 0);
  esp_rom_delay_us(10);
  gpio_set_level(scl, 1);
  esp_rom_delay_us(10);
  gpio_set_level(sda, 1);
  esp_rom_delay_us(10);
}

#ifdef O3CHORDS_KEYPAD_TEST
static char keypad_label(uint8_t index) {
  static constexpr char kLabels[16] = {
      '1', '2', '3', 'A',
      '4', '5', '6', 'B',
      '7', '8', '9', 'C',
      '*', '0', '#', 'D',
  };
  if (index >= 16) return '?';
  return kLabels[index];
}
#endif

extern "C" void app_main(void) {
  static const char* TAG = "MAIN";
  ESP_LOGI(TAG, "MCU boot: reset reason=%d", static_cast<int>(esp_reset_reason()));

#ifdef O3CHORDS_KEYPAD_TEST
  ESP_LOGW(TAG, "Booting in O3CHORDS_KEYPAD_TEST mode");

  // I2C + OLED (optional; test still works over serial if OLED is missing)
  I2cBus i2c;
  I2cBusConfig i2c_cfg;
  i2c_cfg.port = config::kI2cPort;
  i2c_cfg.sda = config::kI2cSda;
  i2c_cfg.scl = config::kI2cScl;
  i2c_cfg.hz = config::kI2cHz;
  recover_i2c_lines(i2c_cfg.sda, i2c_cfg.scl);
  i2c.begin(i2c_cfg);

  OledSsd13xx oled;
  OledConfig oled_cfg;
  oled_cfg.port = i2c.port();
  oled_cfg.addr = config::kOledI2cAddr;
  oled_cfg.col_offset = 2;  // SH1106 (1.3") offset; set to 0 for SSD1306/SSD1315 (0.96")
  bool oled_ok = begin_oled_with_fallback(oled, oled_cfg);

  // Keypad
  KeypadMatrix keypad;
  KeypadMatrixConfig kp_cfg;
  for (int i = 0; i < 4; ++i) {
    kp_cfg.rows[i] = config::kKeypadRows[i];
    kp_cfg.cols[i] = config::kKeypadCols[i];
  }
  keypad.begin(kp_cfg);

  ESP_LOGI(TAG, "Keypad pins rows=%d,%d,%d,%d cols=%d,%d,%d,%d (debounce=%ums settle=%uus)",
           (int)kp_cfg.rows[0], (int)kp_cfg.rows[1], (int)kp_cfg.rows[2], (int)kp_cfg.rows[3],
           (int)kp_cfg.cols[0], (int)kp_cfg.cols[1], (int)kp_cfg.cols[2], (int)kp_cfg.cols[3],
           (unsigned)kp_cfg.debounce_ms, (unsigned)kp_cfg.settle_us);
  ESP_LOGI(TAG, "Index map: [1 2 3 A]=0..3 [4 5 6 B]=4..7 [7 8 9 C]=8..11 [* 0 # D]=12..15");

  std::vector<KeypadEvent> events;
  events.reserve(8);

  char last_key = '-';
  bool last_pressed = false;
  uint32_t last_oled = 0;
  uint16_t last_raw_report = 0xFFFF;
  uint32_t last_raw_report_ms = 0;

  while (true) {
    keypad.poll(events);
    const uint32_t t = now_ms();
    const uint16_t raw = keypad.raw_mask();
    if (raw != last_raw_report && (t - last_raw_report_ms) >= 20) {
      const unsigned c0 = (unsigned)keypad.col_rows_mask(0);
      const unsigned c1 = (unsigned)keypad.col_rows_mask(1);
      const unsigned c2 = (unsigned)keypad.col_rows_mask(2);
      const unsigned c3 = (unsigned)keypad.col_rows_mask(3);
      ESP_LOGI("KEYPAD", "STATE raw=0x%04X stable=0x%04X colrows=[%X %X %X %X]",
               (unsigned)raw,
               (unsigned)keypad.stable_mask(),
               c0, c1, c2, c3);
      last_raw_report = raw;
      last_raw_report_ms = t;
    }

    for (const auto& ev : events) {
      last_key = keypad_label(ev.index);
      last_pressed = ev.pressed;
      const unsigned row = (unsigned)(ev.index / 4);
      const unsigned col = (unsigned)(ev.index % 4);
      const unsigned c0 = (unsigned)keypad.col_rows_mask(0);
      const unsigned c1 = (unsigned)keypad.col_rows_mask(1);
      const unsigned c2 = (unsigned)keypad.col_rows_mask(2);
      const unsigned c3 = (unsigned)keypad.col_rows_mask(3);
      ESP_LOGI("KEYPAD", "%c %s (idx=%u r=%u c=%u) raw=0x%04X stable=0x%04X colrows=[%X %X %X %X]",
               last_key,
               ev.pressed ? "DOWN" : "UP",
               (unsigned)ev.index,
               row,
               col,
               (unsigned)keypad.raw_mask(),
               (unsigned)keypad.stable_mask(),
               c0, c1, c2, c3);
    }

    if (oled_ok && (t - last_oled) > 50) {
      oled.clear();

      char line[64];
      oled.draw_text_line(0, "KEYPAD TEST");
      std::snprintf(line, sizeof(line), "ST:0x%04X", (unsigned)keypad.stable_mask());
      oled.draw_text_line(1, line);
      std::snprintf(line, sizeof(line), "RW:0x%04X", (unsigned)keypad.raw_mask());
      oled.draw_text_line(2, line);
      std::snprintf(line, sizeof(line), "LAST:%c %s", last_key, last_pressed ? "DN" : "UP");
      oled.draw_text_line(3, line);

      const uint16_t st = keypad.stable_mask();
      auto ch = [&](uint8_t idx) { return ((st >> idx) & 1u) ? 'X' : '.'; };
      std::snprintf(line, sizeof(line), "123A %c%c%c%c", ch(0), ch(1), ch(2), ch(3));
      oled.draw_text_line(4, line);
      std::snprintf(line, sizeof(line), "456B %c%c%c%c", ch(4), ch(5), ch(6), ch(7));
      oled.draw_text_line(5, line);
      std::snprintf(line, sizeof(line), "789C %c%c%c%c", ch(8), ch(9), ch(10), ch(11));
      oled.draw_text_line(6, line);
      std::snprintf(line, sizeof(line), "*0#D %c%c%c%c", ch(12), ch(13), ch(14), ch(15));
      oled.draw_text_line(7, line);

      oled.flush();
      last_oled = t;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
#else

  Storage storage;
  storage.begin();

  static app::AppState app_state;
  const auto loaded_config = storage.load_config();
  if (loaded_config.status == app::contract::LoadStatus::Valid ||
      loaded_config.status == app::contract::LoadStatus::Migrated) {
    app_state.set_config(loaded_config.config);
    if (!app::config::apply_to_settings(loaded_config.config, app_state.settings())) {
      ESP_LOGE(TAG, "Persisted config rejected during runtime projection");
    }
  } else if (loaded_config.status != app::contract::LoadStatus::Missing) {
    ESP_LOGW(TAG, "Persisted config unavailable; autosave remains blocked (%u)",
             static_cast<unsigned>(loaded_config.status));
  }
  app_state.sanitize();

  // I2C + OLED
  I2cBus i2c;
  I2cBusConfig i2c_cfg;
  i2c_cfg.port = config::kI2cPort;
  i2c_cfg.sda = config::kI2cSda;
  i2c_cfg.scl = config::kI2cScl;
  i2c_cfg.hz = config::kI2cHz;
  recover_i2c_lines(i2c_cfg.sda, i2c_cfg.scl);
  i2c.begin(i2c_cfg);

  // I2C scan: log every address that ACKs so we can see what's really on the bus.
  ESP_LOGI(TAG, "I2C scan begin...");
  int i2c_found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    uint8_t dummy = 0;
    if (i2c_master_write_to_device(i2c.port(), a, &dummy, 1, pdMS_TO_TICKS(10)) == ESP_OK) {
      ESP_LOGI(TAG, "I2C device found at 0x%02X", a);
      ++i2c_found;
    }
  }
  ESP_LOGI(TAG, "I2C scan done, %d device(s) found", i2c_found);

  static OledSsd13xx oled;
  OledConfig oled_cfg;
  oled_cfg.port = i2c.port();
  oled_cfg.addr = config::kOledI2cAddr;
  oled_cfg.col_offset = 2;  // SH1106 (1.3") offset; set to 0 for SSD1306/SSD1315 (0.96")
  bool oled_ok = begin_oled_with_fallback(oled, oled_cfg);
  uint32_t last_oled_init_try = now_ms();

  // Keypad
  KeypadMatrix keypad;
  KeypadMatrixConfig kp_cfg;
  for (int i = 0; i < 4; ++i) {
    kp_cfg.rows[i] = config::kKeypadRows[i];
    kp_cfg.cols[i] = config::kKeypadCols[i];
  }
  keypad.begin(kp_cfg);

  // Boot-time DFU escape: hold key D (index 15, bottom-right corner) at power-on.
  // Useful when the physical BOOT+RESET buttons are inaccessible.
  {
    vTaskDelay(pdMS_TO_TICKS(80));  // let matrix settle after begin()
    std::vector<KeypadEvent> boot_ev;
    keypad.poll(boot_ev);
    ESP_LOGI(TAG, "Keypad idle scan: raw=0x%04X", (unsigned)keypad.raw_mask());
    if (keypad.raw_mask() & (1u << 15)) {
      ESP_LOGW(TAG, "Key D held at boot — entering USB download mode");
      reboot_to_dfu(oled, oled_ok);
    }
  }

  // Joystick
  JoystickAnalog joystick;
  JoystickAnalogConfig joy_cfg;
  joy_cfg.adc_unit = config::kJoyAdcUnit;
  joy_cfg.adc_channel_x = config::kJoyChanX;
  joy_cfg.adc_channel_y = config::kJoyChanY;
  joy_cfg.pin_x = (int)config::kJoyPinX;
  joy_cfg.pin_y = (int)config::kJoyPinY;
  joy_cfg.pin_sw = (int)config::kJoyPinSw;
  joystick.begin(joy_cfg);

  // Rotary encoder
  RotaryEncoder rotary;
  RotaryEncoderConfig rotary_cfg;
  rotary_cfg.pin_a = config::kRotaryPinA;
  rotary_cfg.pin_b = config::kRotaryPinB;
  rotary_cfg.pin_sw = config::kRotaryPinSw;
  rotary_cfg.invert_direction = config::kRotaryInvert;
  rotary.begin(rotary_cfg);

  GpioButtons ui_buttons;
  GpioButtonsConfig button_cfg;
  button_cfg.button1 = config::kUiButton1;
  button_cfg.button2 = config::kUiButton2;
  ui_buttons.begin(button_cfg);

  // Engine + progression
  // These objects live for the entire firmware lifetime. Keep their fixed
  // buffers off the main task stack, leaving room for MIDI and UI call chains.
  static engine::Engine engine;
  static Router router;
  static RouterLedgerSink ledger_sink(router);
  static engine::NoteLedger note_ledger(&ledger_sink);
  engine.begin();
  engine.set_router(&router);
  // Engine and direct joystick producers share one note authority.
  engine.set_note_ledger(&note_ledger);
  engine.performer().set_strum_direction(engine::StrumDirection::kUp);

  // Seed 7 diatonic chords for the scale (I-VII)
  engine::ProgressionStep s1{1, 0, app_state.settings().velocity, false, "maj", 0, 0};
  engine::ProgressionStep s2{2, 0, app_state.settings().velocity, false, "min", 0, 0};
  engine::ProgressionStep s3{3, 0, app_state.settings().velocity, false, "min", 0, 0};
  engine::ProgressionStep s4{4, 0, app_state.settings().velocity, false, "maj", 0, 0};
  engine::ProgressionStep s5{5, 0, app_state.settings().velocity, false, "maj", 0, 0};
  engine::ProgressionStep s6{6, 0, app_state.settings().velocity, false, "min", 0, 0};
  engine::ProgressionStep s7{7, 0, app_state.settings().velocity, false, "dim", 0, 0};
  engine.progression().set_steps({s1, s2, s3, s4, s5, s6, s7});

  // MIDI
  BleMidi      ble(router);
  UsbDeviceMidi usb(router);
  UsbHostMidi   usb_host(router);
  router.begin();

  // Wire MIDI clock receiver to all transports
  ble.set_midi_clock(&engine.midi_clock());
  usb.set_midi_clock(&engine.midi_clock());
  usb_host.set_midi_clock(&engine.midi_clock());

  apply_settings(app_state, engine, ble, usb);

  // USB OTG role is applied exactly once at the reboot boundary.  Runtime
  // settings changes update desired only and expose restart_required().
  app_state.usb_role_state().apply_at_boot();

  // BLE is started by apply_settings() above (BleMidi::enable brings the
  // stack up on its first enable).

  // USB OTG can run as device (o3Chords → DAW) OR as host (receive from external
  // MIDI device), but not both simultaneously.  usb_host_midi_enabled takes
  // precedence and disables USB device mode for the lifetime of this boot.
  if (app_state.usb_role_state().applied() == midi::UsbRole::Host) {
    usb_host.enable(true);
    usb_host.begin();
  } else if (app_state.usb_role_state().applied() == midi::UsbRole::Device) {
    usb.enable(true);
    usb.begin();
  }
  engine.set_route_mask(router.active_route_mask());

  // Tell receiving synth to use the configured pitch bend range (RPN 0x0000).
  {
    const uint8_t br = app_state.settings().bend_range_semitones;
    for (uint8_t ch : {app_state.settings().midi_channel,
                       app_state.settings().midi_channel_joy}) {
      router.control_change(ch, 101, 0);   // RPN MSB
      router.control_change(ch, 100, 0);   // RPN LSB
      router.control_change(ch, 6, br);    // Data Entry MSB = semitones
      router.control_change(ch, 38, 0);    // Data Entry LSB = 0 cents
    }
  }

  // Input runtime
  static JoystickState js;
  UiNavState nav;
  app::prod::UiState prod_ui;
  std::vector<engine::ProgressionStep> play_mode_progression_steps;

  std::vector<KeypadEvent> events;
  events.reserve(8);
  std::vector<RotaryEvent> rotary_events;
  rotary_events.reserve(8);
  std::vector<GpioButtonEvent> button_events;
  button_events.reserve(4);

  ESP_LOGI(TAG, "Ready");

  // Initialize progression mode (and scale) from the persisted Scale
  // parameter. Scale selects both the scale steps and the 7 degree
  // qualities, so there is nothing left to re-apply from the loaded config
  // afterward.
  nav.prog_mode = static_cast<app::ProgressionMode>(app_state.config().scale);
  apply_progression_mode(engine, nav.prog_mode, app_state.settings());
  apply_settings(app_state, engine, ble, usb);
  play_mode_progression_steps = engine.progression().steps();

  // Restore persisted UI state that lives in UiNavState rather than engine.
  nav.key_nav_mode = static_cast<app::KeyNavMode>(app_state.settings().key_nav_mode);


  InputContext ctx {
    app_state, engine, router, ble, usb,
    js, nav, play_mode_progression_steps, 0, &note_ledger
  };

  app::prod::begin(prod_ui, ctx);
  uint32_t last_save_attempt = 0;
  uint8_t oled_transfer_failures = 0;

  while (true) {
    const uint32_t t = now_ms();
    ctx.t = t;

    // Keypad events
    keypad.poll(events);
    const uint16_t raw = keypad.raw_mask();
    (void)raw;
    app::prod::process_keypad_events(ctx, prod_ui, events);

    // Rotary encoder events
    rotary.poll(rotary_events, t);
    for (const auto& ev : rotary_events) {
      switch (ev.type) {
        case RotaryEventType::kTurn:
          app::prod::encoder_turn(ctx, prod_ui, ev.delta);
          break;
        case RotaryEventType::kClick:
          app::prod::encoder_click(ctx, prod_ui);
          break;
        case RotaryEventType::kLongClick:
          app::prod::encoder_long_click(ctx, prod_ui);
          break;
      }
    }

    // CON and encoder push are equivalent; BAK navigates back, or in PLAY
    // steps the PLAY cursor backward (button_bak() - distinct from the keypad
    // `*` tap's own button_back(), which stays a no-op in PLAY).
    ui_buttons.poll(button_events, t);
    for (const auto& ev : button_events) {
      if (ev.id == GpioButtonId::kButton1) {
        if (ev.type == GpioButtonEventType::kLongClick) {
          app::prod::encoder_long_click(ctx, prod_ui);
        } else {
          app::prod::encoder_click(ctx, prod_ui);
        }
      } else {
        app::prod::button_bak(ctx, prod_ui);
      }
    }

    // Joystick
    if (prod_ui.calibration_open) {
      if (prod_ui.calibration_requested) {
        prod_ui.calibration_requested = false;
        joystick.start_calibration();
        prod_ui.calibration_result = drivers::CalibrationResult::Sampling;
      }
      if (prod_ui.calibration_result == drivers::CalibrationResult::Sampling) {
        prod_ui.calibration_result = joystick.poll_calibration(prod_ui.calibration_progress);
        nav.dirty = true;
      }
      prod_ui.calibration_x = joystick.center_x();
      prod_ui.calibration_y = joystick.center_y();
      prod_ui.calibration_zone = joystick.neutral_zone();
      joystick.get_xy(js.raw_x, js.raw_y);
      nav.dirty = true;
    } else {
      joystick.cancel_calibration();
      process_joystick(ctx, joystick, joy_cfg);
    }

    // Repeat mode: auto-retrigger chord while held
    if (app_state.settings().play_trigger == app::PlayTrigger::kRepeat &&
        nav.holding && nav.active_degree >= 1 && nav.repeat_next_ms != 0 &&
        static_cast<int32_t>(t - nav.repeat_next_ms) >= 0) {
      engine.trigger_step(static_cast<size_t>(nav.active_degree - 1),
                          engine::TriggerMode::kOneShot);
      nav.repeat_next_ms = t + app_state.settings().note_length_ms;
      nav.dirty = true;
    }

    // DFU reboot requested from system menu (confirmed by user)
    if (prod_ui.dfu_reboot_pending) {
      reboot_to_dfu(oled, oled_ok);
    }

    // Plain reboot requested from system menu (confirmed by user)
    if (prod_ui.reboot_pending) {
      if (oled_ok) {
        oled.clear();
        oled.draw_text_line(0, "o3CHORDS");
        oled.draw_text_line(1, "RESTARTING...");
        oled.flush();
      }
      vTaskDelay(pdMS_TO_TICKS(600));
      esp_restart();
    }

    // Service MIDI + engine
    engine.set_route_mask(router.active_route_mask());
    engine.loop();
    const uint16_t clock_ticks = engine.consume_clock_ticks();
    if (!prod_ui.calibration_open) service_joystick_ticks(ctx, clock_ticks);

    router.loop();
    ble.loop();
    usb.loop();
    usb_host.loop();

    // Reset is the user's decision to replace whatever is stored, including
    // settings this firmware could not read.
    if (prod_ui.storage_overwrite_requested) {
      prod_ui.storage_overwrite_requested = false;
      storage.allow_overwrite();
    }
    prod_ui.storage_blocked = storage.writes_blocked();

    // Persist settings after short idle
    if (nav.settings_dirty && (t - nav.last_settings_change) > 1000 &&
        (t - last_save_attempt) > 1000) {
      last_save_attempt = t;
      const uint32_t save_revision = nav.settings_revision;
      auto snapshot = app_state.config();
      snapshot.revision = save_revision;
      const auto save_result = storage.save_config(snapshot);
      if (save_result.status == app::contract::SaveStatus::Saved &&
          nav.settings_revision == save_revision) {
        nav.settings_dirty = false;
      } else if (save_result.status == app::contract::SaveStatus::Unsupported) {
        // Blocked (see Storage::writes_blocked): retrying every second cannot
        // succeed. Drop it; the System screen says NO SAVE and Reset clears it.
        nav.settings_dirty = false;
        ESP_LOGW(TAG, "Settings not saved: stored settings are unreadable; use System > Reset");
      } else {
        ESP_LOGW(TAG, "Config save failed or became stale; retry pending (%u)",
                 static_cast<unsigned>(save_result.status));
      }
    }

    // A display missing at boot is retried without disturbing normal runtime.
    const uint32_t oled_retry_ms = oled_transfer_failures == 3 ? 250 : kOledRetryMs;
    if (!oled_ok && (t - last_oled_init_try) > oled_retry_ms) {
      last_oled_init_try = t;
      i2c.reset();
      recover_i2c_lines(i2c_cfg.sda, i2c_cfg.scl);
      i2c.begin(i2c_cfg);
      oled_ok = begin_oled_with_fallback(oled, oled_cfg, false);
      oled_transfer_failures = oled_ok ? 0 : 4;
      nav.dirty = true;
    }

    // Refresh the joystick scope at ~20Hz, including motion within a sector.
    // Other screens use dirty updates and a slow status keepalive.
    const bool ui_due = (t - nav.last_ui) > 50;
    const bool ui_keepalive = (t - nav.last_ui) > 1000;
    if (oled_ok && ui_due && (nav.dirty || ui_keepalive || (prod_ui.joystick_mode &&
        prod_ui.navigation_mode == app::contract::NavigationMode::Play))) {
      const ProductionRenderCtx rctx { ctx, prod_ui };
      if (!render_production_screen(oled, rctx)) {
        // A transient NACK must not erase the screen and stall it for five
        // seconds. flush() invalidates the frame and restores addressing
        // before the next refresh retransmits all pages.
        nav.last_ui = now_ms();
        nav.dirty = true;
        if (oled_transfer_failures < 0xFF) ++oled_transfer_failures;
        // Do not run the controller's init sequence after a transient NACK:
        // it begins with DISPLAY OFF, so a second failure halfway through can
        // leave an otherwise healthy OLED permanently black. flush() already
        // invalidates its cache and performs a complete recovery frame on the
        // next attempt while the rest of the firmware remains responsive.
        ESP_LOGW(TAG, "OLED transfer failure; retrying without display reset");
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      // Start the interval after the transfer, which can exceed 50ms at 100kHz.
      oled_transfer_failures = 0;
      nav.last_ui = now_ms();
      nav.dirty = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
#endif
}
