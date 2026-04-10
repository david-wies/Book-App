cmake_minimum_required(VERSION 3.24)
project(DigitalLibraryHub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ubuntu/Debian Qt6 install path fallback
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake")
find_package(Qt6 COMPONENTS Widgets Sql REQUIRED)

add_library(shared STATIC
    src/shared/database.cpp
)
target_include_directories(shared PUBLIC src/shared)
target_link_libraries(shared PUBLIC Qt6::Sql)

add_executable(classic-books-gui
    src/gui/main.cpp
)
target_link_libraries(classic-books-gui PRIVATE Qt6::Widgets shared)

add_executable(classic-books-collector
    src/collector/main.cpp
)
target_link_libraries(classic-books-collector PRIVATE Qt6::Core shared)
