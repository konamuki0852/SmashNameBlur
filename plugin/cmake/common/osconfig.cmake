# CMake operating system bootstrap module (Windows only)

include_guard(GLOBAL)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "This plugin builds on Windows only.")
endif()

set(CMAKE_C_EXTENSIONS FALSE)
set(CMAKE_CXX_EXTENSIONS FALSE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows")
set(OS_WINDOWS TRUE)
