cmake_minimum_required(VERSION 3.21)

# 关闭 VS 自带的 vcpkg 集成：它会按 VCPKG_ROOT 向链接行注入 x64-windows 动态库
# （/LIBPATH + "*.lib"），把静态链接产物拖进 DLL 依赖与符号冲突。
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false")

project(ImageEditor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一静态 CRT（vcpkg x64-windows-static 与 Skia 都使用 /MT）
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 产物目录：skia_copy_icudata 会把 icudtl.dat 拷到这里
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# 依赖定位（全部静态库，产物不依赖任何第三方 DLL）
#   libvips：冻结在本项目私有的 E:\lib\ImageEditor（只放 vips.lib + vips 头文件）
#     别的项目重装或改特性的 vcpkg libvips 影响不到本项目
#   其余依赖（glib/jpeg/png/webp/tiff/lcms/opencv/hwy）：取自 vcpkg x64-windows-static，
#     这些库不随 libvips 变动，无需冻结
#   laf：随仓库源码编译，Skia 后端（源码不随包分发，见下方 URL）
# ---------------------------------------------------------------------------
set(VCPKG_STATIC "E:/Programs/vcpkg/installed/x64-windows-static" CACHE PATH "vcpkg 静态 triplet 安装目录")
set(VIPS_LOCAL "E:/lib/libvips/libvips-ImageEditor" CACHE PATH "本项目私有的 libvips 副本（vips.lib + 头文件）")
set(SKIA_DIR "E:/lib/Skia/Skia-Windows-Release-x64" CACHE PATH "" FORCE)
# 第三方源码不进源码包，按需取回后放回下面两个目录：
#   eigen  gitlab.com/libeigen/eigen
#   laf    github.com/aseprite/laf
set(LAF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/laf" CACHE PATH "laf 源码目录（项目内副本）")
set(EIGEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen" CACHE PATH "eigen 头文件目录（项目内副本）")

list(APPEND CMAKE_PREFIX_PATH "${VCPKG_STATIC}")

# 这里不再有 OpenCV：编解码归 libvips，描边/投影用的距离变换与高斯模糊
# 由 core/LayerStyle.cpp 自己两遍扫描算（倒角距离 + 可分离高斯）。

# libvips 的静态链接依赖（vcpkg 的 tiff 用 #pragma comment(lib, "deflate")，
# 而 vcpkg 把这份库命名成 deflatestatic.lib；仓库内给一个同名副本，链接器才找得到）。
set(VIPS_SHIM_DIR "${CMAKE_SOURCE_DIR}/cmake/vips-shim")
set(VIPS_LIBS
    "${VIPS_LOCAL}/lib/vips.lib"
    "${VCPKG_STATIC}/lib/glib-2.0.lib"
    "${VCPKG_STATIC}/lib/gobject-2.0.lib"
    "${VCPKG_STATIC}/lib/gio-2.0.lib"
    "${VCPKG_STATIC}/lib/gmodule-2.0.lib"
    "${VCPKG_STATIC}/lib/intl.lib"
    "${VCPKG_STATIC}/lib/iconv.lib"
    "${VCPKG_STATIC}/lib/charset.lib"
    "${VCPKG_STATIC}/lib/ffi.lib"
    "${VCPKG_STATIC}/lib/libexpatMT.lib"
    "${VIPS_SHIM_DIR}/deflate.lib"
    "${VCPKG_STATIC}/lib/libwebp.lib"
    "${VCPKG_STATIC}/lib/libwebpdemux.lib"
    "${VCPKG_STATIC}/lib/libwebpmux.lib"
    "${VCPKG_STATIC}/lib/jpeg.lib"
    "${VCPKG_STATIC}/lib/tiff.lib"
    "${VCPKG_STATIC}/lib/lcms2.lib"
    "${VCPKG_STATIC}/lib/lzma.lib"
    "${VCPKG_STATIC}/lib/bz2.lib"
    "${VCPKG_STATIC}/lib/zlib.lib"
    # glib 的正则与 webp 的 YUV 转换在 glib/webp 之后解析：
    # 静态链接是单遍扫描，被依赖的库必须排在依赖方后面，否则只会看到"未解析"。
    "${VCPKG_STATIC}/lib/libsharpyuv.lib"
    "${VCPKG_STATIC}/lib/pcre2-8.lib")

# 静态 glib 必须告诉它"在静态库里"，否则符号会被当成 dllimport 处理。
set(VIPS_DEFINITIONS GLIB_STATIC_COMPILATION GOBJECT_STATIC_COMPILATION)

add_library(hwy STATIC IMPORTED)
set_target_properties(hwy PROPERTIES
    IMPORTED_LOCATION "${VCPKG_STATIC}/lib/hwy.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_STATIC}/include"
    INTERFACE_COMPILE_DEFINITIONS "HWY_STATIC_DEFINE"
)

# ---------------------------------------------------------------------------
# LAF（Skia 后端）：窗口、事件队列、Surface、文件对话框
# ---------------------------------------------------------------------------
set(LAF_WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LAF_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(LAF_WITH_CLIP OFF CACHE BOOL "" FORCE)
set(LAF_WITH_IME ON CACHE BOOL "" FORCE)
# 用 Skia 自带的 zlib（CMake 4 下带 FORCE 的 CACHE 变量不会覆盖同名普通变量）
set(ZLIB_LIBRARIES "${SKIA_DIR}/out/Release-x64/zlib.lib")
# FindSkia 用 find_library 找 opengl32，Windows SDK 路径下常找不到；直接给链接名。
set(SKIA_OPENGL_LIBRARY opengl32 CACHE FILEPATH "" FORCE)
set(LAF_BACKEND skia CACHE STRING "" FORCE)
add_subdirectory("${LAF_DIR}" "${CMAKE_BINARY_DIR}/laf")

# ---------------------------------------------------------------------------
# 本体
# ---------------------------------------------------------------------------
add_executable(ImageEditor WIN32
    app.rc
    src/main.cpp
    src/core/Image.cpp
    src/core/LocalWarp.cpp
    src/core/Document.cpp
    src/core/Matting.cpp
    src/core/Selection.cpp
    src/core/History.cpp
    src/core/Blend.cpp
    src/core/ShadowLift.cpp
    src/core/Primaries.cpp
    src/core/LayerStyle.cpp
    src/core/Clipboard.cpp
    src/core/TextRender.cpp
    src/core/Transform.cpp
    src/core/ImageIo.cpp
    src/core/Ora.cpp
    src/core/ClassRonin_QRData.cpp
    src/app/Editor.cpp
    src/app/LocalWarpEdit.cpp
    src/tools/Tool.cpp
    src/tools/MoveTool.cpp
    src/tools/LassoTool.cpp
    src/tools/WandTool.cpp
    src/tools/BrushTool.cpp
    src/tools/EraserTool.cpp
    src/tools/MaskTool.cpp
    src/tools/BucketTool.cpp
    src/tools/GradientTool.cpp
    src/tools/ShapeTool.cpp
    src/tools/SelectTool.cpp
    src/tools/CropTool.cpp
    src/tools/TextTool.cpp
    src/nodes/Node.cpp
    src/nodes/Container.cpp
    src/nodes/Widgets.cpp
    src/nodes/CanvasNode.cpp
    src/nodes/OverlayRecord.cpp
    src/nodes/Panels.cpp
    src/nodes/Dialogs.cpp
    src/host/Compositor.cpp
    src/host/Host.cpp
    src/host/Shortcuts.cpp
)


set(OPENCV_LIBS
    "${VCPKG_STATIC}/lib/opencv_core4.lib"
    "${VCPKG_STATIC}/lib/opencv_imgproc4.lib"
)

target_include_directories(ImageEditor PRIVATE src
    "${SKIA_DIR}/third_party/externals/freetype/include"
    "${VCPKG_STATIC}/include/opencv4"
    "${EIGEN_DIR}"
    "${VIPS_LOCAL}/include"
    "${VCPKG_STATIC}/include"
    "${VCPKG_STATIC}/include/glib-2.0"
    "${VCPKG_STATIC}/lib/glib-2.0/include")
target_link_libraries(ImageEditor PRIVATE
    ${OPENCV_LIBS}
    "${VCPKG_STATIC}/lib/libpng16.lib"
    laf-os laf-text laf-dlgs laf-gfx laf-base
    ${VIPS_LIBS}
    hwy
    "${SKIA_DIR}/out/Release-x64/freetype2.lib"
    shell32 ole32 comdlg32 user32 gdi32 dwmapi ws2_32 userenv bcrypt advapi32 shlwapi iphlpapi dnsapi)

target_compile_definitions(ImageEditor PRIVATE
    ${VIPS_DEFINITIONS}
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    HWY_STATIC_DEFINE
)

# ---------------------------------------------------------------------------
# 自检夹具：main.cpp 的 ICC 断言读取 exe 同目录的 icc_test_cmyk.jpg。
# 权威副本放在 tools/fixtures，构建后自动复制到输出目录；
# 清理 bin 目录之后自检不再因为"缺图"断掉（见 ronin/职责边界.md 第九节）。
# ---------------------------------------------------------------------------
add_custom_command(TARGET ImageEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures/icc_test_cmyk.jpg"
            "$<TARGET_FILE_DIR:ImageEditor>/icc_test_cmyk.jpg"
    COMMENT "复制自检夹具 icc_test_cmyk.jpg")

# A/B 对拍工具：走与主程序完全相同的 imageio 路径
add_executable(abconv tools/abconv.cpp src/core/ImageIo.cpp src/core/Image.cpp)
target_include_directories(abconv PRIVATE src
    "${VIPS_LOCAL}/include"
    "${VCPKG_STATIC}/include"
    "${VCPKG_STATIC}/include/glib-2.0"
    "${VCPKG_STATIC}/lib/glib-2.0/include")
target_compile_options(abconv PRIVATE /utf-8 /W3)
target_link_libraries(abconv PRIVATE "${VCPKG_STATIC}/lib/libpng16.lib" ${VIPS_LIBS} ws2_32 userenv bcrypt advapi32 shlwapi iphlpapi dnsapi)
target_compile_definitions(abconv PRIVATE ${VIPS_DEFINITIONS} NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_link_libraries(abconv PRIVATE comdlg32 user32)
    set_target_properties(abconv PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(MSVC)
    target_compile_options(ImageEditor PRIVATE /W3 /permissive- /utf-8 /EHsc)
    # skia.lib 内含一份 libpng，vcpkg opencv 又硬编码 libpng16：
    # 两者同名 png_* 符号，只保留先出现的一份（libpng16，1.6.x ABI 兼容）。
    target_link_options(ImageEditor PRIVATE /FORCE:MULTIPLE)
    set_property(TARGET ImageEditor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
