# Xbox 360 Racing Wheel to ELRS Transmitter Bridge
# ESP32-S3 + USB Host + CRSF

cmake_minimum_required(VERSION 3.16)

# Include local overrides if present (for WiFi credentials, etc.)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(xbox-elrs)
