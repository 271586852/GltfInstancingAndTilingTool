cmake_minimum_required(VERSION 3.16)

project(GltfInstancingTool LANGUAGES CXX)

# ----------------------------------------------------------------------------------
#  基础 C++17
# ----------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "C++ Standard set to 17.")

# ----------------------------------------------------------------------------------
#  构建配置 (强制 Debug)
# ----------------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default build type" FORCE)
  message(STATUS "CMAKE_BUILD_TYPE forced to Debug for single-configuration generator.")
endif()
set(CMAKE_CONFIGURATION_TYPES Debug CACHE STRING "Supported configs" FORCE) # 只允许 Debug
message(STATUS "Effective CMAKE_BUILD_TYPE for single-config generator: ${CMAKE_BUILD_TYPE}")
message(STATUS "Supported CMAKE_CONFIGURATION_TYPES for multi-config generator: ${CMAKE_CONFIGURATION_TYPES}")

# ----------------------------------------------------------------------------------
#  Cesium Native v0.37.0 (Debug - 手动指定路径和库)
# ----------------------------------------------------------------------------------
set(CESIUM_NATIVE_V037_DEBUG_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/cesium-native/install_debug_v0.37.0")
set(CESIUM_NATIVE_V037_DEBUG_INCLUDE_DIR "${CESIUM_NATIVE_V037_DEBUG_ROOT_DIR}/include")
set(CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR "${CESIUM_NATIVE_V037_DEBUG_ROOT_DIR}/lib")

message(STATUS "Cesium Native v0.37.0 Debug Root Dir: ${CESIUM_NATIVE_V037_DEBUG_ROOT_DIR}")
message(STATUS "Cesium Native v0.37.0 Debug Include Dir: ${CESIUM_NATIVE_V037_DEBUG_INCLUDE_DIR}")
message(STATUS "Cesium Native v0.37.0 Debug Library Dir: ${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}")

if(NOT IS_DIRECTORY "${CESIUM_NATIVE_V037_DEBUG_INCLUDE_DIR}")
    message(FATAL_ERROR "Cesium Native v0.37.0 Debug include directory NOT FOUND: ${CESIUM_NATIVE_V037_DEBUG_INCLUDE_DIR}")
endif()
if(NOT IS_DIRECTORY "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}")
    message(FATAL_ERROR "Cesium Native v0.37.0 Debug library directory NOT FOUND: ${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}")
endif()

# ----------------------------------------------------------------------------------
#  源文件
# ----------------------------------------------------------------------------------
set(PROJECT_SOURCES
    src/main.cpp
    src/glb_reader.cpp
    src/instancing_detector.cpp
    src/glb_writer.cpp
    src/tileset_writer.cpp
    src/utilities.cpp
    
    #src/utils.cpp
    #src/tileset_generator.cpp
)
add_executable(gltf_instancer ${PROJECT_SOURCES})
message(STATUS "Executable 'gltf_instancer' added.")

# ----------------------------------------------------------------------------------
#  编译选项 / 宏
# ----------------------------------------------------------------------------------
target_compile_definitions(gltf_instancer PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    # 对于 Debug 构建，定义 _DEBUG (CMake 通常会自动为 MSVC Debug 配置定义)
    $<$<CONFIG:Debug>:_DEBUG>
)
message(STATUS "Compile definitions set for 'gltf_instancer'.")

if(MSVC)
    target_compile_options(gltf_instancer PRIVATE /utf-8 /bigobj)
    # 强制 Debug 运行时库 (/MDd)
    set_property(TARGET gltf_instancer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")
    message(STATUS "MSVC: Runtime library for 'gltf_instancer' set to MultiThreadedDebugDLL.")
    # MSVC Debug 模式下，当 _DEBUG 定义时，_ITERATOR_DEBUG_LEVEL 默认为 2
endif()

# ----------------------------------------------------------------------------------
#  头文件搜索路径
# ----------------------------------------------------------------------------------
target_include_directories(gltf_instancer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CESIUM_NATIVE_V037_DEBUG_INCLUDE_DIR}"
)
message(STATUS "Include directories for 'gltf_instancer' set to:")
message(STATUS "  - ${CMAKE_CURRENT_SOURCE_DIR}/src")
message(STATUS "  - ${CESIUM_NATIVE_V037_DEBUG_INCLUDE_DIR}")

# ----------------------------------------------------------------------------------
#  手动指定要链接的 Cesium Native v0.37.0 Debug 库
#  文件名基于你提供的 install_debug_v0.37.0/lib 目录列表
# ----------------------------------------------------------------------------------
set(CESIUM_NATIVE_MANUAL_DEBUG_LIBS_V037
    # --- Cesium Native 核心组件库 (Debug 版本) ---
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/Cesium3DTilesContentd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/Cesium3DTilesd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/Cesium3DTilesReaderd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/Cesium3DTilesSelectiond.lib" # 按需
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/Cesium3DTilesWriterd.lib"   # 按需
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumAsyncd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumGeometryd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumGeospatiald.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumGltfContentd.lib" # 按需
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumGltfd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumGltfReaderd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumGltfWriterd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumIonClientd.lib" # 按需
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumJsonReaderd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumJsonWriterd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumQuantizedMeshTerraind.lib" # 按需
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumRasterOverlaysd.lib"     # 按需
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/CesiumUtilityd.lib"
    # 注意：根据你的列表，v0.37.0 的确为每个 Cesium 模块生成了带 'd' 后缀的库

    # --- 第三方依赖库 (Debug 版本, 根据你的列表) ---
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/async++d.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/csprngd.lib" # 这个库在之前的列表里没有，但 v0.37.0 可能包含
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/dracod.lib"  # ** 链接 Draco Debug 库 **
    # fmt.lib 没有出现在你的 v0.37.0 列表中，如果 Cesium Native v0.37.0 不依赖 fmt，或者 fmt 是仅头文件版本，则不需要链接。
    # 如果链接时报 fmt 相关的 LNK2019，你需要找到 fmt 的 Debug 库并添加。
    # "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/fmtd.lib" # 假设需要，但你的列表没有它
    # glm.lib 也没有出现在你的 v0.37.0 列表中，GLM 通常是头文件库。
    # "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/glmd.lib"  # 假设需要
    # jpeg.lib 和 turbojpeg.lib (或其Debug版本) 也没有在你的 v0.37.0 列表中。
    # 如果需要 JPEG 支持，你需要找到 turbojpegd.lib (或类似名称) 并链接。
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/ktxd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/modp_b64d.lib" # 你的列表是 modp_b64d.lib
    # libsharpyuv.lib, libwebp.lib 等 WebP 相关库没有出现在你的 v0.37.0 列表中。
    # 如果需要 WebP 支持，你需要找到对应的 Debug 版本并链接。例如 webpdecoderd.lib
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/webpdecoderd.lib" # 保持，因为你列出了它
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/meshoptimizerd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/s2geometryd.lib" # 你的列表是 s2geometryd.lib
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/spdlogd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/sqlite3d.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/tinyxml2d.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/turbojpegd.lib" # 加入这个，如果 ImageDecoder 需要
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/uriparserd.lib"
    "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/zlibstaticd.lib" # 你的列表是 zlibstaticd.lib
    # zstd.lib 没有出现在你的 v0.37.0 列表中。如果 KTX 或其他库需要 ZStandard，你需要找到 zstdd.lib 并链接。
    # "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/zstdd.lib"

    # OpenSSL - v0.37.0 的 install_debug/lib 列表中没有 libcrypto/libssl，
    # 这可能意味着 v0.37.0 使用了不同的网络/加密库，或者这些功能是可选的，或者被静态链接到其他库中了。
    # 如果遇到网络相关的 LNK2019，需要调查 v0.37.0 使用的是什么。
    # "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/libcryptod.lib"
    # "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/libssld.lib"

    # ASTC - v0.37.0 的 install_debug/lib 列表中没有 astcenc，可能不支持或集成方式不同。
    # "${CESIUM_NATIVE_V037_DEBUG_LIBRARY_DIR}/astcenc-avx2-staticd.lib"

    # Abseil 库 - v0.37.0 的 install_debug/lib 列表中没有 absl_* 库。
    # 这可能意味着 s2geometryd.lib 静态链接了 Abseil，或者 v0.37.0 使用了不同版本的 S2。
)

message(STATUS "Manually specified Cesium Native v0.37.0 Debug libraries to link:")
foreach(lib_path ${CESIUM_NATIVE_MANUAL_DEBUG_LIBS_V037})
    if(EXISTS "${lib_path}")
        message(STATUS "  [FOUND] ${lib_path}")
    else()
        message(WARNING "  [MISSING] ${lib_path} - This library will cause a linker error if required.")
    endif()
endforeach()

target_link_libraries(gltf_instancer
    PRIVATE
        ${CESIUM_NATIVE_MANUAL_DEBUG_LIBS_V037}
        # --- Windows 系统库 ---
        winhttp.lib # CesiumAsync 可能需要
        crypt32.lib # 如果用到加密
        ws2_32.lib  # 网络
        rpcrt4.lib
        shlwapi.lib
        bcrypt.lib  # 如果用到加密
        iphlpapi.lib # 网络
        DbgHelp.lib # spdlog 可能需要
)
message(STATUS "Libraries linked against 'gltf_instancer'.")

# ----------------------------------------------------------------------------------
#  其他设置
# ----------------------------------------------------------------------------------
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "")
set_property(TARGET gltf_instancer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

message(STATUS "CMake configuration for 'gltf_instancer' (Debug only, for Cesium Native v0.37.0) finished.")