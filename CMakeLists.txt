cmake_minimum_required(VERSION 3.24)
project(DigitalLibraryHub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Ubuntu/Debian Qt6 install path fallback
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake")
find_package(Qt6 COMPONENTS Widgets Sql Network REQUIRED)

add_library(shared STATIC
    src/shared/database.cpp
)
target_include_directories(shared PUBLIC src/shared)
target_link_libraries(shared PUBLIC Qt6::Sql Qt6::Network)

add_executable(classic-books
    src/main.cpp
    src/collector/collector_worker.cpp
    src/collector/source_adapter.cpp
    src/collector/gutenberg_adapter.cpp
    src/collector/book_discovery_service.cpp
)
target_link_libraries(classic-books PRIVATE Qt6::Widgets Qt6::Network shared)

# Copy icon next to the executable after every build (mirrors database co-location pattern)
add_custom_command(TARGET classic-books POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/book_reader_icon.jpg"
        "$<TARGET_FILE_DIR:classic-books>/book_reader_icon.jpg"
    COMMENT "Copying book_reader_icon.jpg next to executable"
)
