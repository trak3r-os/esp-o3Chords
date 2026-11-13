cmake_minimum_required(VERSION 3.16)
project(o3Chords_Tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(O3_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# ---------------------------------------------------------------------------
# Stub include path comes BEFORE the real src/ so it satisfies ESP-IDF headers.
# ---------------------------------------------------------------------------
set(STUB_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
set(SRC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../src)

include_directories(
    ${STUB_DIR}          # ESP-IDF / FreeRTOS stubs
    ${SRC_DIR}           # project source headers
)

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# Stub globals (one translation unit that owns extern variables)
# ---------------------------------------------------------------------------
set(STUB_SOURCES
    ${STUB_DIR}/stub_globals.cpp
    ${STUB_DIR}/midi_stubs.cpp
    ${STUB_DIR}/drivers_stubs.cpp
    ${SRC_DIR}/drivers/oled_framebuffer.cpp
)

# ---------------------------------------------------------------------------
# Engine / app source files that can compile on the host
# (all pure-logic files — no hardware calls in their paths)
# ---------------------------------------------------------------------------
set(ENGINE_SOURCES
    ${SRC_DIR}/engine/chords.cpp
    ${SRC_DIR}/engine/progression.cpp
    ${SRC_DIR}/engine/arpeggiator.cpp
    ${SRC_DIR}/engine/performer.cpp
    ${SRC_DIR}/engine/modifier.cpp
    ${SRC_DIR}/engine/midi_clock.cpp
    ${SRC_DIR}/engine/midi_scheduler.cpp
    ${SRC_DIR}/engine/engine.cpp
    ${SRC_DIR}/engine/music_theory.cpp
    ${SRC_DIR}/engine/chord_substitutions.cpp
    ${SRC_DIR}/engine/pitch_bend.cpp
    ${SRC_DIR}/engine/note_ledger.cpp
    ${SRC_DIR}/midi/router.cpp
    ${SRC_DIR}/app/settings_bridge.cpp
    ${SRC_DIR}/app/config_codec.cpp
    ${SRC_DIR}/app/parameters.cpp
    ${SRC_DIR}/app/commands.cpp
    ${SRC_DIR}/app/ui_model.cpp
    ${SRC_DIR}/app/progression_modes.cpp
    ${SRC_DIR}/app/harmony_transform.cpp
    ${SRC_DIR}/app/modulation_recipe.cpp
    ${SRC_DIR}/app/voice_recipe.cpp
    ${SRC_DIR}/app/state.cpp
    ${SRC_DIR}/app/joy_family.cpp
    ${SRC_DIR}/ui/display_names.cpp
)

# Additional sources needed by param-dispatch and UI-state tests
set(APP_SOURCES
    ${SRC_DIR}/app/input_context.cpp
    ${SRC_DIR}/app/input_joystick.cpp
    ${SRC_DIR}/app/production_ui.cpp
    ${SRC_DIR}/ui/production_render.cpp
)

# ---------------------------------------------------------------------------
# Individual test executables
# ---------------------------------------------------------------------------

function(add_test_exe name)
    add_executable(${name}
        ${ARGN}
        ${STUB_SOURCES}
        ${ENGINE_SOURCES}
    )
    target_compile_definitions(${name} PRIVATE NATIVE_TEST=1)
endfunction()

function(add_test_exe_full name)
    add_executable(${name}
        ${ARGN}
        ${STUB_SOURCES}
        ${ENGINE_SOURCES}
        ${APP_SOURCES}
    )
    target_compile_definitions(${name} PRIVATE NATIVE_TEST=1)
endfunction()

add_test_exe(test_chord_building      unit/test_chord_building.cpp)
add_test_exe(test_oled_stub_pixels    unit/test_oled_stub_pixels.cpp)
add_test_exe_full(test_golden_snapshots unit/test_golden_snapshots.cpp)
add_test_exe(test_joy_family          unit/test_joy_family.cpp)
add_test_exe(test_midi_routing        unit/test_midi_routing.cpp)
add_test_exe(test_midi_scheduler      unit/test_midi_scheduler.cpp)
add_test_exe(test_config_codec        unit/test_config_codec.cpp)
add_executable(test_storage
    unit/test_storage.cpp
    ${STUB_DIR}/stub_globals.cpp
    ${SRC_DIR}/drivers/storage.cpp
    ${SRC_DIR}/app/config_codec.cpp
    ${SRC_DIR}/app/state.cpp
    ${SRC_DIR}/app/joy_family.cpp)
target_compile_definitions(test_storage PRIVATE NATIVE_TEST=1)
target_include_directories(test_storage PRIVATE ${STUB_DIR} ${SRC_DIR})
add_test_exe(test_parameters           unit/test_parameters.cpp)
add_test_exe(test_commands             unit/test_commands.cpp)
add_test_exe(test_note_ledger          unit/test_note_ledger.cpp)
add_test_exe(test_ui_model             unit/test_ui_model.cpp)
add_test_exe(test_arpeggiator         unit/test_arpeggiator.cpp)
add_test_exe(test_service_held        unit/test_service_held.cpp)
add_test_exe(test_strum_rhythm           unit/test_strum_rhythm.cpp)
add_test_exe(test_progression_modes     unit/test_progression_modes.cpp)
add_test_exe(test_pitch_bend            unit/test_pitch_bend.cpp)
add_test_exe(test_music_theory_analytics unit/test_music_theory_analytics.cpp)
add_test_exe(test_chord_substitutions   unit/test_chord_substitutions.cpp)
add_test_exe_full(test_modifier_performer unit/test_modifier_performer.cpp)
add_test_exe_full(test_joystick           unit/test_joystick.cpp)
add_test_exe_full(test_joystick_modes     unit/test_joystick_modes.cpp)
add_test_exe_full(test_joy_session        unit/test_joy_session.cpp)
add_test_exe(test_midi_clock_external     unit/test_midi_clock_external.cpp)
add_test_exe_full(test_sanitize           unit/test_sanitize.cpp)
add_test_exe_full(test_joystick_note_modes           unit/test_joystick_note_modes.cpp)
add_test_exe_full(test_joystick_analog_modes         unit/test_joystick_analog_modes.cpp)
add_test_exe_full(test_joystick_bend_interval_penta  unit/test_joystick_bend_interval_penta.cpp)
add_test_exe_full(test_joystick_preview_compass      unit/test_joystick_preview_compass.cpp)
add_test_exe(test_harmony_transform            unit/test_harmony_transform.cpp)
add_test_exe(test_modulation_recipe            unit/test_modulation_recipe.cpp)
add_test_exe(test_voice_recipe                unit/test_voice_recipe.cpp)
add_test_exe_full(test_drone_ledger_admission        unit/test_drone_ledger_admission.cpp)
add_test_exe_full(test_navigation                    unit/test_navigation.cpp)
add_test_exe_full(test_ui_snapshot                   unit/test_ui_snapshot.cpp)
add_test_exe_full(test_keypad_joystick               unit/test_keypad_joystick.cpp)
add_test_exe_full(test_voice_ownership               unit/test_voice_ownership.cpp)
add_test_exe_full(test_config_revision               unit/test_config_revision.cpp)
add_test_exe_full(test_joystick_presets              unit/test_joystick_presets.cpp)
add_test_exe_full(test_production_ui                 unit/test_production_ui.cpp)
add_test_exe_full(test_info_menu                     unit/test_info_menu.cpp)

# Development aid: prints the real renderer's screens to the terminal.
add_test_exe_full(screen_dump tools/screen_dump.cpp)

# ---------------------------------------------------------------------------
# CTest integration
# ---------------------------------------------------------------------------
enable_testing()
foreach(t
    test_chord_building
    test_oled_stub_pixels
    test_golden_snapshots
    test_joy_family
    test_midi_routing
    test_midi_scheduler
    test_config_codec
    test_storage
    test_parameters
    test_commands
    test_note_ledger
    test_ui_model
    test_arpeggiator
    test_service_held
    test_strum_rhythm
    test_progression_modes
    test_pitch_bend
    test_music_theory_analytics
    test_chord_substitutions
    test_modifier_performer
    test_joystick
    test_joystick_modes
    test_joy_session
    test_midi_clock_external
    test_sanitize
    test_joystick_note_modes
    test_joystick_analog_modes
    test_joystick_bend_interval_penta
    test_joystick_preview_compass
    test_drone_ledger_admission
    test_navigation
    test_ui_snapshot
    test_keypad_joystick)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
add_test(NAME test_harmony_transform COMMAND test_harmony_transform)
add_test(NAME test_modulation_recipe COMMAND test_modulation_recipe)
add_test(NAME test_voice_recipe COMMAND test_voice_recipe)
add_test(NAME test_voice_ownership COMMAND test_voice_ownership)
add_test(NAME test_config_revision COMMAND test_config_revision)
add_test(NAME test_joystick_presets COMMAND test_joystick_presets)
add_test(NAME test_production_ui COMMAND test_production_ui)
add_test(NAME test_info_menu COMMAND test_info_menu)
