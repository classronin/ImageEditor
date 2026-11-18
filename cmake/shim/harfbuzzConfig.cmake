# 本机 vcpkg 的 harfbuzz 静态配置强制要求未安装的 ICU。
# 我们只用到 opencv_core / imgproc / imgcodecs / photo，这些目标并不链接 harfbuzz，
# 因此这里提供一个空接口目标满足 find_dependency(harfbuzz CONFIG) 即可。
if(NOT TARGET harfbuzz::harfbuzz)
  add_library(harfbuzz::harfbuzz INTERFACE IMPORTED)
endif()
set(harfbuzz_FOUND TRUE)
