cmake_minimum_required(VERSION 3.15)
project(LoMo C CXX)

# Compatibility with LZ4/GLFW CMake
if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()
if(POLICY CMP0072)
  cmake_policy(SET CMP0072 NEW) # OpenGL policy
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# LZ4
FetchContent_Declare(
  lz4
  GIT_REPOSITORY https://github.com/lz4/lz4.git
  GIT_TAG v1.10.0
  SOURCE_SUBDIR build/cmake
)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.8
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.89.9
)

FetchContent_MakeAvailable(lz4 glfw imgui)

# Platform-specific configurations
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(HAL_SRC src/engine/os_win.c)
    set(PLATFORM_LIBS Cabinet.lib dwmapi.lib imm32.lib)
elseif(APPLE)
    set(HAL_SRC src/engine/os_posix.c)
    set(PLATFORM_LIBS pthread "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
else()
    set(HAL_SRC src/engine/os_posix.c)
    set(PLATFORM_LIBS pthread dl)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND PLATFORM_LIBS uring)
    endif()
endif()

# OpenGL
find_package(OpenGL REQUIRED)

# Include directories
include_directories(src/engine)
include_directories(src/ui)
include_directories(${lz4_SOURCE_DIR}/lib)
include_directories(${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# Common engine sources
set(ENGINE_COMMON_SRC
    src/engine/storage_engine.c
    src/engine/simd_filter.c
    ${HAL_SRC}
)

# Executables
add_executable(lomo_cli
    src/engine/engine_main.c
    src/engine/ingestor.c
    src/engine/sql_parser.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(lomo_cli ${PLATFORM_LIBS} lz4_static)

add_executable(scanner
    src/engine/scanner_core.c
    src/engine/ingestor.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(scanner ${PLATFORM_LIBS} lz4_static)

add_executable(test_ingest
    src/engine/test_ingestion.c
    src/engine/ingestor.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(test_ingest ${PLATFORM_LIBS} lz4_static)

add_executable(query_lomo
    src/engine/query_engine.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(query_lomo ${PLATFORM_LIBS} lz4_static)

add_executable(test_merge
    src/engine/test_merge.c
    src/engine/ingestor.c
    src/engine/merger.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(test_merge ${PLATFORM_LIBS} lz4_static)

add_executable(lomo_sql
    src/engine/test_sql.c
    src/engine/sql_parser.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(lomo_sql ${PLATFORM_LIBS} lz4_static)

add_executable(benchmark_ingest
    src/engine/benchmark_ingest.c
    src/engine/ingestor.c
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(benchmark_ingest ${PLATFORM_LIBS} lz4_static)

# LoMo UI (Dear ImGui)
set(IMGUI_SRC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(lomo_ui 
    src/ui/ui_main.cpp 
    src/ui/ui_render.cpp
    src/ui/document.c
    src/ui/editor.c
    ${IMGUI_SRC} 
    ${ENGINE_COMMON_SRC}
)
target_link_libraries(lomo_ui glfw OpenGL::GL ${PLATFORM_LIBS} lz4_static)

# Copy test data and dummy logs for local execution
add_custom_command(TARGET lomo_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/engine/test_part_dir
    $<TARGET_FILE_DIR:lomo_cli>/test_part_dir
)
add_custom_command(TARGET lomo_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/engine/merged_part
    $<TARGET_FILE_DIR:lomo_cli>/merged_part
)
add_custom_command(TARGET lomo_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/src/engine/dummy_web.log
    $<TARGET_FILE_DIR:lomo_cli>/dummy_web.log
)
