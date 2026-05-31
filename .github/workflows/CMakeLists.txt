cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Geode setup ────────────────────────────────────────────────────────────────
# Убедись, что переменная GEODE_SDK задана (или передай -DGEODE_SDK=...)
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Переменная окружения GEODE_SDK не задана. "
            "Установи Geode CLI и выполни 'geode sdk install'.")
endif()
file(TO_CMAKE_PATH $ENV{GEODE_SDK} GEODE_SDK)
include(${GEODE_SDK}/cmake/GeodeFile.cmake)

project(PlatformerProgress VERSION 1.0.0)

# ── Исходники ──────────────────────────────────────────────────────────────────
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

# ── nlohmann/json (header-only, тянем через FetchContent) ─────────────────────
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/_deps/nlohmann
)
FetchContent_MakeAvailable(nlohmann_json)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/_deps/nlohmann)

# ── Geode линковка ─────────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} geode-sdk)
create_geode_file(${PROJECT_NAME})
