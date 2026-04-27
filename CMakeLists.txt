cmake_minimum_required(VERSION 3.28)
project(BookHub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Sql Network Test REQUIRED)
find_package(LibArchive REQUIRED)

# shared: pure database helpers, no Q_OBJECT — safe as a STATIC library consumed by all targets.
add_library(shared STATIC
    src/shared/database.cpp
)
target_include_directories(shared PUBLIC src/shared)
target_link_libraries(shared PUBLIC Qt6::Sql)

# Collector and GUI sources are compiled directly into the consuming target rather than as a
# separate STATIC library. Qt AUTOMOC + STATIC libraries causes CMake to recompile all sources
# inline for every consumer while also omitting the library's moc_compilation unit from the
# link, leaving Q_OBJECT vtables/staticMetaObjects undefined at link time.
set(COLLECTOR_SOURCES
    src/collector/collector_worker.cpp
    src/collector/source_adapter.cpp
    src/collector/gutenberg_adapter.cpp
    src/collector/book_discovery_service.cpp
)

set(GUI_SOURCES
    src/gui/main_window.cpp
    src/gui/screens/library_screen.cpp
    src/gui/widgets/book_card_delegate.cpp
    src/gui/widgets/empty_state_widget.cpp
    src/gui/widgets/badge_label.cpp
    src/gui/screens/search_screen.cpp
    src/gui/widgets/search_result_delegate.cpp
    src/gui/services/library_service.cpp
    src/gui/services/search_service.cpp
    src/gui/screens/explore_screen.cpp
    src/gui/services/explore_service.cpp
    src/gui/query_worker.cpp
)

add_executable(bookhub
    src/main.cpp
    resources.qrc
    ${COLLECTOR_SOURCES}
    ${GUI_SOURCES}
)
target_include_directories(bookhub PRIVATE src)
target_link_libraries(bookhub PRIVATE Qt6::Widgets Qt6::Network shared LibArchive::LibArchive)

enable_testing()

function(add_bookhub_test test_name)
    add_executable(${test_name} ${ARGN} ${COLLECTOR_SOURCES} ${GUI_SOURCES})
    target_include_directories(${test_name} PRIVATE src tests)
    target_link_libraries(${test_name} PRIVATE Qt6::Test Qt6::Widgets Qt6::Network shared LibArchive::LibArchive)
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
