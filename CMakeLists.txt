cmake_minimum_required(VERSION 3.28)
project(BookHub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------------------------
# Sanitizer options — each builds in its own directory to avoid flag conflicts
#   cmake -B build-san  -DENABLE_SANITIZERS=ON  ..  (ASan + UBSan, most runs)
#   cmake -B build-tsan -DENABLE_TSAN=ON         ..  (TSan, race detection)
#   cmake -B build-msan -DENABLE_MSAN=ON         ..  (MSan, clang only)
# ---------------------------------------------------------------------------
option(ENABLE_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# TSan is incompatible with ASan and MSan — run in a dedicated build tree.
option(ENABLE_TSAN "Enable ThreadSanitizer (incompatible with ASan/MSan)" OFF)
if(ENABLE_TSAN)
    if(ENABLE_SANITIZERS OR ENABLE_MSAN)
        message(FATAL_ERROR "ENABLE_TSAN is incompatible with ENABLE_SANITIZERS and ENABLE_MSAN")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# MSan requires a clang build and an MSan-instrumented C++ standard library.
option(ENABLE_MSAN "Enable MemorySanitizer (clang only, incompatible with ASan/TSan)" OFF)
if(ENABLE_MSAN)
    if(ENABLE_SANITIZERS OR ENABLE_TSAN)
        message(FATAL_ERROR "ENABLE_MSAN is incompatible with ENABLE_SANITIZERS and ENABLE_TSAN")
    endif()
    add_compile_options(-fsanitize=memory -fno-omit-frame-pointer -fPIE)
    add_link_options(-fsanitize=memory -pie)
endif()

find_package(Qt6 COMPONENTS Widgets Sql Network REQUIRED)
find_package(Qt6 COMPONENTS Multimedia QUIET)
find_package(LibArchive REQUIRED)

# Qt6::Test is only needed when the test suite is built — keep release/packaging
# builds free of a hard dependency on it. The actual find_package(... Test ...)
# call lives inside the if(BUILD_TESTING) block below.

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
    src/gui/services/native_tts_service.cpp
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

# ---------------------------------------------------------------------------
# GPU / accelerator backend — auto-detected, no opt-in required
# ---------------------------------------------------------------------------
# Exactly one backend macro is defined; the first match in this priority
# chain wins.  Task 22 maps each macro to the correct ONNX execution-
# provider string when wiring real synthesis.
#
# Priority and scope:
#   1. BOOKHUB_HAVE_CUDA     — NVIDIA CUDA ≥11        (Linux, Windows)
#   2. BOOKHUB_HAVE_COREML   — Apple CoreML           (macOS, always present)
#   3. BOOKHUB_HAVE_DIRECTML — Windows DirectML       (Windows 10+, all GPU vendors)
#   4. BOOKHUB_HAVE_ROCM     — AMD ROCm / HIP         (Linux)
#   5. BOOKHUB_HAVE_OPENVINO — Intel OpenVINO         (Linux, Windows, macOS)
#   6. (none)                — CPU fallback
#
# BOOKHUB_HAVE_GPU is always defined alongside the specific macro so code
# that only needs to guard any GPU path can use a single check.
#
# Only the TTS engines that target ONNX execution providers consume these
# macros, so detection only runs when at least one such engine is enabled.
# Default builds (both engines OFF) skip it entirely.
#
# IMPORTANT: the TTS library (Sherpa-ONNX / ONNX Runtime) must itself be
# built with the matching backend — detecting the toolkit is necessary but
# not sufficient for GPU inference to work at runtime.
if(ENABLE_SHERPA_ONNX OR ENABLE_POCKET_TTS)
    find_package(CUDAToolkit 11 QUIET)
    if(CUDAToolkit_FOUND)
        # NVIDIA CUDA — highest priority on all platforms that support it.
        message(STATUS "CUDA ${CUDAToolkit_VERSION} found — GPU inference via CUDA execution provider (BOOKHUB_HAVE_CUDA)")
        target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_GPU BOOKHUB_HAVE_CUDA)
    elseif(APPLE)
        # CoreML is always available on Apple platforms — no external toolkit needed.
        message(STATUS "Apple platform — GPU inference via CoreML execution provider (BOOKHUB_HAVE_COREML)")
        target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_GPU BOOKHUB_HAVE_COREML)
    elseif(WIN32)
        # DirectML ships with Windows 10+ and covers all GPU vendors (AMD, Intel, NVIDIA)
        # via DirectX 12 without requiring a vendor-specific SDK.
        message(STATUS "Windows platform — GPU inference via DirectML execution provider (BOOKHUB_HAVE_DIRECTML)")
        target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_GPU BOOKHUB_HAVE_DIRECTML)
    else()
        # Linux without CUDA: try AMD ROCm, then Intel OpenVINO, then CPU.
        find_package(hip QUIET HINTS /opt/rocm/lib/cmake/hip /usr/lib/cmake/hip)
        if(hip_FOUND)
            message(STATUS "ROCm/HIP ${hip_VERSION} found — GPU inference via ROCm execution provider (BOOKHUB_HAVE_ROCM)")
            target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_GPU BOOKHUB_HAVE_ROCM)
        else()
            # Intel OpenVINO supports iGPU and CPU neural-network acceleration.
            find_package(OpenVINO QUIET)
            if(OpenVINO_FOUND)
                message(STATUS "OpenVINO ${OpenVINO_VERSION} found — inference via OpenVINO execution provider (BOOKHUB_HAVE_OPENVINO)")
                target_compile_definitions(bookhub_gui PUBLIC BOOKHUB_HAVE_GPU BOOKHUB_HAVE_OPENVINO)
            else()
                message(STATUS "No GPU/accelerator toolkit found — TTS engines will use CPU inference")
            endif()
        endif()
    endif()
endif() # ENABLE_SHERPA_ONNX OR ENABLE_POCKET_TTS

# ---------------------------------------------------------------------------
# Precompiled headers — amortise large Qt header parsing across all TUs.
# Each OBJECT library gets its own PCH because their compile definitions differ
# (bookhub_gui picks up BOOKHUB_HAVE_MULTIMEDIA, GPU macros, etc.).
# ---------------------------------------------------------------------------
target_precompile_headers(bookhub_collector PRIVATE
    <QObject>
    <QString>
    <QStringList>
    <QList>
    <QMap>
    <QByteArray>
    <QUrl>
    <QSqlDatabase>
    <QSqlQuery>
    <QSqlError>
    <QNetworkAccessManager>
    <QNetworkReply>
    <QNetworkRequest>
)
target_precompile_headers(bookhub_gui PRIVATE
    <QObject>
    <QString>
    <QStringList>
    <QList>
    <QMap>
    <QWidget>
    <QDialog>
    <QLabel>
    <QPushButton>
    <QVBoxLayout>
    <QHBoxLayout>
    <QSqlDatabase>
    <QSqlQuery>
    <QSqlError>
)

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

# ---------------------------------------------------------------------------
# Test suite — disable with -DBUILD_TESTING=OFF to skip all test targets,
# e.g. for release packaging where no test code should be compiled.
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build and register the test suite" ON)
if(BUILD_TESTING)
    find_package(Qt6 COMPONENTS Test REQUIRED)
    enable_testing()

    # FULL_SUITE_ONLY marks a test EXCLUDE_FROM_ALL so it is not built by the
    # default 'all' target. CI builds these explicitly only on master before
    # running the full ctest suite. Omit the flag for any test in the 'sanity'
    # label set, which must be available after every plain 'make'.
    #
    # Each FULL_SUITE_ONLY test is also registered as a dependency of the
    # 'bookhub_full_suite_tests' aggregate target, which CI builds in a single
    # step. Adding a new FULL_SUITE_ONLY test therefore wires it into CI
    # automatically — no parallel list in ci.yml to keep in sync.
    add_custom_target(bookhub_full_suite_tests
        COMMENT "Building all FULL_SUITE_ONLY test executables")

    function(add_bookhub_test test_name)
        cmake_parse_arguments(ARG "FULL_SUITE_ONLY" "" "" ${ARGN})
        add_executable(${test_name} ${ARG_UNPARSED_ARGUMENTS})
        if(ARG_FULL_SUITE_ONLY)
            set_target_properties(${test_name} PROPERTIES EXCLUDE_FROM_ALL TRUE)
            add_dependencies(bookhub_full_suite_tests ${test_name})
        endif()
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

    add_bookhub_test(test_database_helpers
        tests/unit/test_database_helpers.cpp
    )
    label_bookhub_test(test_database_helpers sanity unit database)

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

    add_bookhub_test(test_library_screen FULL_SUITE_ONLY
        tests/gui/test_library_screen.cpp
    )
    label_bookhub_test(test_library_screen gui library)

    add_bookhub_test(test_search_service FULL_SUITE_ONLY
        tests/integration/test_search_service.cpp
    )
    label_bookhub_test(test_search_service integration search)

    add_bookhub_test(test_search_screen FULL_SUITE_ONLY
        tests/gui/test_search_screen.cpp
    )
    label_bookhub_test(test_search_screen gui search)

    add_bookhub_test(test_explore_service FULL_SUITE_ONLY
        tests/integration/test_explore_service.cpp
    )
    label_bookhub_test(test_explore_service integration explore)

    add_bookhub_test(test_explore_screen FULL_SUITE_ONLY
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

    add_bookhub_test(test_wav_utils
        tests/unit/test_wav_utils.cpp
    )
    label_bookhub_test(test_wav_utils sanity unit tts)

endif() # BUILD_TESTING

# ---------------------------------------------------------------------------
# clang-format target — reformat all sources in-place
#   cmake --build build --target format
# ---------------------------------------------------------------------------
# find runs at build time so newly added files are picked up without re-running cmake.
find_program(CLANG_FORMAT_EXE NAMES clang-format-19 clang-format-18 clang-format-17 clang-format)
if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND find ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
                \( -name "*.cpp" -o -name "*.h" \)
                -exec ${CLANG_FORMAT_EXE} -i --style=file {} +
        COMMENT "Running clang-format on all sources"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND find ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
                \( -name "*.cpp" -o -name "*.h" \)
                -exec ${CLANG_FORMAT_EXE} --style=file --dry-run -Werror {} +
        COMMENT "Checking clang-format compliance (no files modified)"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found — 'format' and 'format-check' targets unavailable")
endif()
