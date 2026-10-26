cmake_minimum_required(VERSION 3.22)
project(deskray CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenXR REQUIRED CONFIG)   # prefab package from openxr_loader_for_android

add_library(native_app_glue STATIC
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
  ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(deskray SHARED ${SRC})
target_include_directories(deskray PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(deskray PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(deskray
  native_app_glue OpenXR::openxr_loader
  android log EGL GLESv3)
# Keep the glue's entry point: nothing references it but the Android loader.
target_link_options(deskray PRIVATE -u ANativeActivity_onCreate)
