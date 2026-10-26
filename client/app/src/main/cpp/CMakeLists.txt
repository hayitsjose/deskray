cmake_minimum_required(VERSION 3.22)
project(deskray CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party)

find_package(OpenXR REQUIRED CONFIG)   # prefab package from openxr_loader_for_android

add_library(native_app_glue STATIC
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
  ${ANDROID_NDK}/sources/android/native_app_glue)

# Mbed TLS provides the AES-GCM moonlight-common-c needs, without
# cross-compiling OpenSSL. Only the crypto library is built.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY}/mbedtls mbedtls EXCLUDE_FROM_ALL)

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${THIRD_PARTY}/moonlight-common-c moonlight-common-c EXCLUDE_FROM_ALL)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(deskray SHARED ${SRC})
target_include_directories(deskray PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${THIRD_PARTY}/moonlight-common-c/src)
target_compile_options(deskray PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(deskray
  native_app_glue OpenXR::openxr_loader moonlight-common-c
  android log EGL GLESv3)
# Keep the glue's entry point: nothing references it but the Android loader.
target_link_options(deskray PRIVATE -u ANativeActivity_onCreate)
