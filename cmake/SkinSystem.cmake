# Reuse the zlib sources already supplied by FreeType; do not link its UI runtime.
enable_language(C)
set(skin_zlib_dir "${PROJECT_SOURCE_DIR}/third_party/freetype/src/gzip")
if(NOT EXISTS "${skin_zlib_dir}/inflate.c")
    message(FATAL_ERROR "Skin ZIP support needs the existing FreeType submodule (git submodule update --init --recursive).")
endif()
add_library(sevenmania_skin_inflate STATIC "${PROJECT_SOURCE_DIR}/src/skin/SkinInflate.c")
target_include_directories(sevenmania_skin_inflate PUBLIC "${skin_zlib_dir}")
target_compile_definitions(sevenmania_skin_inflate PUBLIC Z_PREFIX)

add_library(sevenmania_skin STATIC
    "${PROJECT_SOURCE_DIR}/src/skin/SkinZip.cpp"
    "${PROJECT_SOURCE_DIR}/src/skin/SkinJson.cpp"
    "${PROJECT_SOURCE_DIR}/src/skin/SkinImage.cpp"
    "${PROJECT_SOURCE_DIR}/src/skin/SkinLoader.cpp"
    "${PROJECT_SOURCE_DIR}/src/skin/SkinManager.cpp")
target_sources(sevenmania_skin PRIVATE "${PROJECT_SOURCE_DIR}/src/skin/SkinData.cpp")
target_include_directories(sevenmania_skin PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(sevenmania_skin PRIVATE sevenmania_skin_inflate windowscodecs ole32)
add_executable(SevenManiaSkinTests "${PROJECT_SOURCE_DIR}/tests/SkinTests.cpp")
target_link_libraries(SevenManiaSkinTests PRIVATE sevenmania_skin)
add_test(NAME SevenManiaSkinTests COMMAND SevenManiaSkinTests "${CMAKE_BINARY_DIR}/Validation/Skins" "${PROJECT_SOURCE_DIR}/tests/fixtures/skins/loader-result.semaskin")
set_tests_properties(SevenManiaSkinTests PROPERTIES TIMEOUT 60)

# Optional screen renderer uses the same Direct2D-on-D3D11 path as CompositionPresenter.
add_library(sevenmania_skin_render STATIC "${PROJECT_SOURCE_DIR}/src/skin/SkinRenderer.cpp" "${PROJECT_SOURCE_DIR}/src/skin/SkinApplication.cpp")
target_link_libraries(sevenmania_skin_render PUBLIC sevenmania_skin PRIVATE sevenmania_skin_inflate d3d11 d2d1 msimg32 windowscodecs ole32 gdi32 user32)
add_executable(SevenManiaSkinRenderTests "${PROJECT_SOURCE_DIR}/tests/SkinRenderTests.cpp")
target_link_libraries(SevenManiaSkinRenderTests PRIVATE sevenmania_skin_render d3d11 d2d1 windowscodecs ole32)
add_test(NAME SevenManiaSkinRenderTests COMMAND SevenManiaSkinRenderTests
    "${PROJECT_SOURCE_DIR}/tests/fixtures/skins/render-layers.semaskin" "${CMAKE_BINARY_DIR}/Validation/SkinRender")
set_tests_properties(SevenManiaSkinRenderTests PROPERTIES TIMEOUT 120)
