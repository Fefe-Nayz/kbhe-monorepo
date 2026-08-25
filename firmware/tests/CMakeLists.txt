cmake_minimum_required(VERSION 3.22)
project(kbhe_firmware_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(KBHE_CORE_INC "${CMAKE_CURRENT_LIST_DIR}/../Core/Inc")
set(KBHE_MONOCYPHER "${CMAKE_CURRENT_LIST_DIR}/../lib/monocypher")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Werror)
endif()

# Keep legacy assert-based tests meaningful even if a developer explicitly
# configures this test project as Release. CI still uses Debug so production
# optimization cannot hide undefined behaviour in the harness itself.
if(MSVC)
  add_compile_options("/FI${CMAKE_CURRENT_LIST_DIR}/test_assertions_active.h")
else()
  add_compile_options(-include "${CMAKE_CURRENT_LIST_DIR}/test_assertions_active.h")
endif()

add_executable(updater_validation_test
  updater_validation_test.c
  ../Core/Src/updater_validation.c
)
target_include_directories(updater_validation_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME updater_validation_test COMMAND updater_validation_test)

add_executable(adc_capture_test
  adc_capture_test.c
  ../Core/Src/adc_capture.c
)
target_include_directories(adc_capture_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME adc_capture_test COMMAND adc_capture_test)

add_executable(analog_rest_estimator_test
  analog_rest_estimator_test.c
  ../Core/Src/analog/rest_estimator.c
)
target_include_directories(analog_rest_estimator_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME analog_rest_estimator_test COMMAND analog_rest_estimator_test)

add_executable(analog_diagnostic_profile_policy_test
  analog_diagnostic_profile_policy_test.c
)
target_include_directories(analog_diagnostic_profile_policy_test PRIVATE
  "${KBHE_CORE_INC}"
)
add_test(NAME analog_diagnostic_profile_policy_test
  COMMAND analog_diagnostic_profile_policy_test
)

add_executable(internal_sensor_sampler_test
  internal_sensor_sampler_test.c
  ../Core/Src/analog/internal_sensor_sampler.c
)
target_include_directories(internal_sensor_sampler_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/internal_sensor_sampler_stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME internal_sensor_sampler_test COMMAND internal_sensor_sampler_test)

add_executable(raw_hid_test
  raw_hid_test.c
  ../Core/Src/hid/raw_hid.c
)
target_include_directories(raw_hid_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME raw_hid_test COMMAND raw_hid_test)

add_executable(consumer_hid_test
  consumer_hid_test.c
  ../Core/Src/hid/consumer_hid.c
)
target_include_directories(consumer_hid_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME consumer_hid_test COMMAND consumer_hid_test)

add_executable(mouse_hid_test
  mouse_hid_test.c
  ../Core/Src/hid/mouse_hid.c
)
target_include_directories(mouse_hid_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME mouse_hid_test COMMAND mouse_hid_test)

add_executable(gamepad_hid_transport_test
  gamepad_hid_transport_test.c
  ../Core/Src/hid/gamepad_hid.c
)
target_include_directories(gamepad_hid_transport_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME gamepad_hid_transport_test COMMAND gamepad_hid_transport_test)

add_executable(xinput_usb_test xinput_usb_test.c)
target_include_directories(xinput_usb_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/xinput_usb_stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME xinput_usb_test COMMAND xinput_usb_test)

add_executable(usb_gamepad_identity_test
  usb_gamepad_identity_test.c
  ../Core/Src/usb_descriptors.c
)
target_include_directories(usb_gamepad_identity_test PRIVATE
  "${KBHE_CORE_INC}"
  "${CMAKE_CURRENT_LIST_DIR}/../lib/tinyusb/src"
)
add_test(NAME usb_gamepad_identity_test COMMAND usb_gamepad_identity_test)

add_executable(keyboard_hid_queue_test
  keyboard_hid_queue_test.c
  ../Core/Src/hid/keyboard_hid.c
  ../Core/Src/hid/keyboard_nkro_hid.c
)
target_include_directories(keyboard_hid_queue_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/keyboard_hid_stubs"
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME keyboard_hid_queue_test COMMAND keyboard_hid_queue_test)

add_executable(flash_storage_codec_test
  flash_storage_codec_test.c
  ../Core/Src/flash_storage_codec.c
)
target_include_directories(flash_storage_codec_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME flash_storage_codec_test COMMAND flash_storage_codec_test)

add_executable(flash_storage_bank_test
  flash_storage_bank_test.c
  ../Core/Src/flash_storage.c
  ../Core/Src/flash_storage_codec.c
  ../Core/Src/profile_document_store.c
)
target_include_directories(flash_storage_bank_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME flash_storage_bank_test COMMAND flash_storage_bank_test)

add_executable(profile_document_store_test
  profile_document_store_test.c
  ../Core/Src/profile_document_store.c
)
target_include_directories(profile_document_store_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME profile_document_store_test COMMAND profile_document_store_test)

add_executable(action_protocol_test
  action_protocol_test.c
  ../Core/Src/action_protocol.c
)
target_include_directories(action_protocol_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME action_protocol_test COMMAND action_protocol_test)

add_executable(transition_filter_test transition_filter_test.c)
target_include_directories(transition_filter_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME transition_filter_test COMMAND transition_filter_test)

add_executable(trigger_threshold_test trigger_threshold_test.c)
target_include_directories(trigger_threshold_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME trigger_threshold_test COMMAND trigger_threshold_test)

add_executable(filter_test
  filter_test.c
  ../Core/Src/analog/filter.c
)
target_include_directories(filter_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/filter_test_stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME filter_test COMMAND filter_test)

add_executable(realtime_scan_policy_test realtime_scan_policy_test.c)
target_include_directories(realtime_scan_policy_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME realtime_scan_policy_test COMMAND realtime_scan_policy_test)

add_executable(socd_test
  socd_test.c
  ../Core/Src/trigger/socd.c
)
target_include_directories(socd_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
)
add_test(NAME socd_test COMMAND socd_test)

add_executable(led_overlay_math_test led_overlay_math_test.c)
target_include_directories(led_overlay_math_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME led_overlay_math_test COMMAND led_overlay_math_test)

add_executable(rotary_tap_scheduler_test rotary_tap_scheduler_test.c)
target_include_directories(rotary_tap_scheduler_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME rotary_tap_scheduler_test COMMAND rotary_tap_scheduler_test)

add_executable(action_engine_test
  action_engine_test.c
  ../Core/Src/action_engine.c
)
target_include_directories(action_engine_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME action_engine_test COMMAND action_engine_test)

add_executable(action_store_async_test
  action_store_async_test.c
  ../Core/Src/action_store.c
)
target_include_directories(action_store_async_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME action_store_async_test COMMAND action_store_async_test)

add_executable(settings_save_policy_test settings_save_policy_test.c)
target_include_directories(settings_save_policy_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME settings_save_policy_test COMMAND settings_save_policy_test)

add_executable(ram_only_protocol_test ram_only_protocol_test.c)
target_include_directories(ram_only_protocol_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME ram_only_protocol_test COMMAND ram_only_protocol_test)

add_executable(rgb_bridge_protocol_test rgb_bridge_protocol_test.c)
target_include_directories(rgb_bridge_protocol_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME rgb_bridge_protocol_test COMMAND rgb_bridge_protocol_test)

add_executable(rgb_frame_staging_test
  rgb_frame_staging_test.c
  ../Core/Src/rgb_frame_staging.c
)
target_include_directories(rgb_frame_staging_test PRIVATE "${KBHE_CORE_INC}")
add_test(NAME rgb_frame_staging_test COMMAND rgb_frame_staging_test)

add_executable(updater_signature_test
  updater_signature_test.c
  "${KBHE_MONOCYPHER}/src/monocypher.c"
  "${KBHE_MONOCYPHER}/src/optional/monocypher-ed25519.c"
)
target_include_directories(updater_signature_test PRIVATE
  "${KBHE_CORE_INC}"
  "${CMAKE_CURRENT_LIST_DIR}/../Bootloader/Inc"
  "${KBHE_MONOCYPHER}/src"
  "${KBHE_MONOCYPHER}/src/optional"
)
target_compile_definitions(updater_signature_test PRIVATE
  RELEASE_SIGNING_VECTOR_PATH="${CMAKE_CURRENT_LIST_DIR}/release_signing_vectors.json"
)
add_test(NAME updater_signature_test COMMAND updater_signature_test)

add_executable(updater_version_floor_test
  updater_version_floor_test.c
  ../Bootloader/Src/updater_version_floor.c
)
target_include_directories(updater_version_floor_test PRIVATE
  "${KBHE_CORE_INC}"
  "${CMAKE_CURRENT_LIST_DIR}/../Bootloader/Inc"
)
target_compile_definitions(updater_version_floor_test PRIVATE
  UPDATER_VERSION_FLOOR_HOST_TEST=1
)
add_test(NAME updater_version_floor_test COMMAND updater_version_floor_test)

add_executable(updater_migrator_plan_test
  updater_migrator_plan_test.c
  ../Migrator/Src/updater_migrator_plan.c
)
target_include_directories(updater_migrator_plan_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../Migrator/Inc"
)
add_test(NAME updater_migrator_plan_test COMMAND updater_migrator_plan_test)

add_executable(updater_bootloader_protocol_test
  updater_bootloader_protocol_test.c
  ../Bootloader/Src/updater_bootloader.c
  ../Bootloader/Src/updater_version_floor.c
  ../Core/Src/updater_shared.c
  ../Core/Src/updater_validation.c
  "${KBHE_MONOCYPHER}/src/monocypher.c"
  "${KBHE_MONOCYPHER}/src/optional/monocypher-ed25519.c"
)
target_include_directories(updater_bootloader_protocol_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stubs"
  "${KBHE_CORE_INC}"
  "${CMAKE_CURRENT_LIST_DIR}/../Bootloader/Inc"
  "${KBHE_MONOCYPHER}/src"
  "${KBHE_MONOCYPHER}/src/optional"
)
target_compile_definitions(updater_bootloader_protocol_test PRIVATE
  UPDATER_HOST_TEST=1
  UPDATER_VERSION_FLOOR_HOST_TEST=1
  RELEASE_SIGNING_VECTOR_PATH="${CMAKE_CURRENT_LIST_DIR}/release_signing_vectors.json"
)
add_test(NAME updater_bootloader_protocol_test COMMAND updater_bootloader_protocol_test)
