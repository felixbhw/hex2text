cmake_minimum_required(VERSION 3.30)
project(Hex2Text C)

set(CMAKE_C_STANDARD 23)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(JSON REQUIRED json-c)

# Include directories
include_directories(${GTK4_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS} ${JSON_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS} ${CURL_LIBRARY_DIRS} ${JSON_LIBRARY_DIRS})
add_definitions(${GTK4_CFLAGS_OTHER} ${CURL_CFLAGS_OTHER} ${JSON_CFLAGS_OTHER})

# Add executable
add_executable(Hex2Text main.c ai_translator.c common.c)

# Link libraries
target_link_libraries(Hex2Text ${GTK4_LIBRARIES} ${CURL_LIBRARIES} ${JSON_LIBRARIES})
