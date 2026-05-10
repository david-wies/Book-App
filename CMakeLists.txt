cmake_minimum_required(VERSION 3.28)
project(BookHub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Sql Network Test REQUIRED)
find_package(Qt6 COMPONENTS Multimedia QUIET)
find_package(LibArchive REQUIRED)

# shared: pure database helpers, no Q_OBJECT — safe as a STATIC library consumed by all targets.
add_library(shared STATIC
    src/shared/database.cpp
)
target_include_directories(shared PUBLIC src/shared)
target_link_libraries(shared PUBLIC Qt6::Sql)

set(COLLECTOR_SOURCES
    src/collector/collector_worker.cpp
    src/collector/source_adapter.cpp
    src/collector/gutenberg_adapter.cpp
    src/collector/book_discovery_service.cpp
)

set(GUI_SOURCES
    src/gui/dialogs/audiobook_flow_dialog.cpp
    src/gui/dialogs/download_flow_dialog.cpp
    src/gui/dialogs/voice_upload_dialog.cpp
    src/gui/main_window.cpp
    src/gui/panels/book_details_panel.cpp
    src/gui/screens/explore_screen.cpp
    src/gui/screens/library_screen.cpp
    src/gui/screens/search_screen.cpp
    src/gui/services/book_details_service.cpp
    src/gui/services/explore_service.cpp
    src/gui/services/library_service.cpp
    src/gui/services/pocket_tts_service.cpp
    src/gui/services/search_service.cpp
    src/gui/services/sherpa_onnx_tts_service.cpp
    src/gui/services/tts_service.cpp
    src/gui/widgets/badge_label.cpp
    src/gui/widgets/book_card_delegate.cpp
    src/gui/widgets/empty_state_widget.cpp
    src/gui/widgets/mini_audio_player_widget.cpp
    src/gui/widgets/search_result_delegate.cpp
    src/gui/widgets/step_indicator_widget.cpp
    src/gui/widgets/voice_selector_widget.cpp
    src/gui/query_worker.cpp
)

# OBJECT libraries share compiled .o files across targets without re-invoking the compiler.
# STATIC libraries cannot be used here: Qt AUTOMOC + STATIC omits the moc_compilation unit
# from the link, leaving Q_OBJECT vtables/staticMetaObjects undefined at link time.
add_library(bookhub_collector OBJECT ${COLLECTOR_SOURCES})
target_include_directories(bookhub_collector PUBLIC src)
target_link_libraries(bookhub_collector PUBLIC Qt6::Widgets Qt6::Network shared LibArchive::LibArchive)

add_library(bookhub_gui OBJECT ${GUI_SOURCES})
target_include_directories(bookhub_gui PUBLIC src)
target_link_libraries(bookhub_gui PUBLIC Qt6::Widgets Qt6::Network shared)

# Optional: Qt Multimedia for in-app audio preview on step 4 of the audiobook flow.
# When present, MiniAudioPlayerWidget is wired to QMediaPlayer for real playback.
# Without it, the preview falls back to opening the system audio player.
if(Qt6Multimedia_FOUND)
    message(STATUS "Qt6::Multimedia found — in-app preview playback enabled")
    target_link_libraries(bookhub_gui PUBLIC Qt6::Multimedia)
    target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_MULTIMEDIA)
endif()

# Optional: Sherpa-ONNX for real preset-voice TTS (Apache 2.0).
# Models are managed by ModelManager (Task 22). Without this option the
# SherpaOnnxTTSService class still compiles but degrades to emitting failure.
option(ENABLE_SHERPA_ONNX "Enable Sherpa-ONNX TTS engine (Apache 2.0)" OFF)
if(ENABLE_SHERPA_ONNX)
    find_package(sherpa-onnx REQUIRED)
    target_link_libraries(bookhub_gui PUBLIC sherpa-onnx)
    target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_SHERPA_ONNX)
    message(STATUS "Sherpa-ONNX TTS enabled")
endif()

# Optional: PocketTTS.cpp for zero-shot voice cloning (MIT).
# Models are managed by ModelManager (Task 22). Without this option the
# PocketTTSService class still compiles but degrades to emitting failure.
option(ENABLE_POCKET_TTS "Enable PocketTTS.cpp voice cloning engine (MIT)" OFF)
if(ENABLE_POCKET_TTS)
    find_package(pocket-tts REQUIRED)
    target_link_libraries(bookhub_gui PUBLIC pocket-tts)
    target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_POCKET_TTS)
    message(STATUS "PocketTTS.cpp voice cloning enabled")
endif()

add_executable(bookhub
    src/main.cpp
    resources.qrc
)
# macOS-specific: creates a .app bundle for packaging. Ignored on Linux and Windows.
if(APPLE)
    set_target_properties(bookhub PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "BookHub"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.david.bookhub"
    )
endif()
# Only hide console on Windows for Release builds — keeps stderr/stdout visible during development.
# Uses a generator expression so this works with both single-config (Ninja/make) and
# multi-config (Visual Studio) generators, where CMAKE_BUILD_TYPE is not set.
set_target_properties(bookhub PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
target_include_directories(bookhub PRIVATE src)
target_link_libraries(bookhub PRIVATE bookhub_collector bookhub_gui)

enable_testing()

function(add_bookhub_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE src tests)
    target_link_libraries(${test_name} PRIVATE Qt6::Test bookhub_collector bookhub_gui)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

function(label_bookhub_test test_name)
    set_property(TEST ${test_name} APPEND PROPERTY LABELS ${ARGN})
endfunction()

add_bookhub_test(test_database_schema
    tests/unit/test_database_schema.cpp
)
label_bookhub_test(test_database_schema sanity unit database)

add_bookhub_test(test_gutenberg_id_resolution
    tests/unit/test_gutenberg_id_resolution.cpp
)
label_bookhub_test(test_gutenberg_id_resolution sanity unit collector identity)

add_bookhub_test(test_book_discovery_service
    tests/integration/test_book_discovery_service.cpp
)
label_bookhub_test(test_book_discovery_service sanity integration collector identity)

add_bookhub_test(test_library_service
    tests/integration/test_library_service.cpp
)
label_bookhub_test(test_library_service sanity integration library)

add_bookhub_test(test_main_window
    tests/gui/test_main_window.cpp
)
label_bookhub_test(test_main_window sanity gui shell)

add_bookhub_test(test_library_screen
    tests/gui/test_library_screen.cpp
)
label_bookhub_test(test_library_screen gui library)

add_bookhub_test(test_search_service
    tests/integration/test_search_service.cpp
)
label_bookhub_test(test_search_service integration search)

add_bookhub_test(test_search_screen
    tests/gui/test_search_screen.cpp
)
label_bookhub_test(test_search_screen gui search)

add_bookhub_test(test_explore_service
    tests/integration/test_explore_service.cpp
)
label_bookhub_test(test_explore_service integration explore)

add_bookhub_test(test_explore_screen
    tests/gui/test_explore_screen.cpp
)
label_bookhub_test(test_explore_screen gui explore)

add_bookhub_test(test_book_details_panel
    tests/gui/test_book_details_panel.cpp
)
label_bookhub_test(test_book_details_panel sanity gui details)

add_bookhub_test(test_download_flow_dialog
    tests/gui/test_download_flow_dialog.cpp
)
label_bookhub_test(test_download_flow_dialog sanity gui download)

add_bookhub_test(test_step_indicator_widget
    tests/gui/test_step_indicator_widget.cpp
)
label_bookhub_test(test_step_indicator_widget sanity gui widgets)

add_bookhub_test(test_audiobook_flow_dialog
    tests/gui/test_audiobook_flow_dialog.cpp
)
label_bookhub_test(test_audiobook_flow_dialog sanity gui audiobook)

add_bookhub_test(test_tts_service
    tests/unit/test_tts_service.cpp
)
label_bookhub_test(test_tts_service sanity unit tts)
