cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(BmsPlayerCpp LANGUAGES CXX)
option(BMS_ENABLE_NVIDIA_REFLEX "Enable optional NVIDIA Reflex driver integration" ON)
if(NOT BMS_ENABLE_NVIDIA_REFLEX)
    add_compile_definitions(BMS_DISABLE_NVIDIA_REFLEX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(/utf-8 /W4 /permissive- /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX)

# Song imports retain nested source directories and add temporary copy names.
# Opt all executables (including integration tests) into Windows long paths.
add_link_options("/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/long-paths.manifest")

enable_testing()

enable_language(RC)

include(cmake/RustSongSelection.cmake)

include(cmake/PerformanceLibraries.cmake)
include(cmake/RmlSongList.cmake)

add_executable(BmsResultFadeTests tests/ResultFadeTests.cpp)
target_include_directories(BmsResultFadeTests PRIVATE src)
add_test(NAME BmsResultFadeTests COMMAND BmsResultFadeTests)

add_executable(BmsGpuTests tests/GpuTests.cpp)
target_include_directories(BmsGpuTests PRIVATE src)
add_test(NAME BmsGpuTests COMMAND BmsGpuTests)
add_executable(BmsLowLatencyTests tests/LowLatencyTests.cpp)
target_include_directories(BmsLowLatencyTests PRIVATE src)
add_test(NAME BmsLowLatencyTests COMMAND BmsLowLatencyTests)
add_executable(BmsPitchStretchTests tests/PitchStretchTests.cpp)
target_include_directories(BmsPitchStretchTests PRIVATE src)
add_test(NAME BmsPitchStretchTests COMMAND BmsPitchStretchTests)

add_library(bms_core STATIC src/BmsParser.cpp src/SongLibrary.cpp src/SongCatalog.cpp src/RustBackend.cpp)
target_sources(bms_core PRIVATE src/Mpeg1Video.cpp)
include(cmake/SkinSystem.cmake)
target_include_directories(bms_core PUBLIC src)

add_library(bms_song_selection STATIC ${BMS_SONG_SELECTION_SOURCE})
target_include_directories(bms_song_selection PUBLIC src)
target_link_libraries(bms_song_selection PUBLIC bms_core)
if(BMS_ENABLE_RUST_SONG_SELECTION)
    target_include_directories(bms_song_selection PRIVATE ${BMS_SONG_SELECTION_INCLUDE_DIRS})
    target_link_libraries(bms_song_selection PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:bms_rust_song_selection_native>"
        "$<$<NOT:$<CONFIG:Debug>>:ws2_32;userenv;ntdll;dbghelp;kernel32>")
endif()

add_executable(BmsPlayer WIN32
    src/Main.cpp
    src/AudioEngine.cpp
    src/SystemResources.cpp
    src/app.rc
)
target_link_libraries(BmsPlayer PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
target_link_libraries(BmsPlayer PRIVATE sevenmania_skin_render)
target_compile_definitions(BmsPlayer PRIVATE BMS_ENABLE_SKIN_BINDINGS)

# Explicitly opt in: a different executable and settings file for the test branch.
add_executable(BmsPlayerTest EXCLUDE_FROM_ALL WIN32 src/Main.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
set_target_properties(BmsPlayerTest PROPERTIES OUTPUT_NAME "BmsPlayer-Test")
target_compile_definitions(BmsPlayerTest PRIVATE BMS_FRAME_DIAGNOSTICS)
target_link_libraries(BmsPlayerTest PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
add_executable(BmsFrameStatisticsTests tests/FrameStatisticsTests.cpp)
target_include_directories(BmsFrameStatisticsTests PRIVATE src)
add_test(NAME BmsFrameStatisticsTests COMMAND BmsFrameStatisticsTests)

foreach(test_asset turntable.png blackbutton.png whitebutton.png hud-icon-slot.png hud-hi-speed.png hud-gauge-mode.png hit-splash.png levelchange.mp3 click.mp3)
    add_custom_command(TARGET BmsPlayerTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:BmsPlayerTest>/Assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Assets/${test_asset} $<TARGET_FILE_DIR:BmsPlayerTest>/Assets/${test_asset})
endforeach()
add_executable(BmsDiagnosticsTests tests/DiagnosticsTests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
target_compile_definitions(BmsDiagnosticsTests PRIVATE BMS_FRAME_DIAGNOSTICS)
target_link_libraries(BmsDiagnosticsTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
add_test(NAME BmsDiagnosticsTests COMMAND BmsDiagnosticsTests ${CMAKE_CURRENT_BINARY_DIR}/Validation/Diagnostics)

add_executable(BmsPlayerProfile EXCLUDE_FROM_ALL WIN32 src/Main.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_compile_definitions(BmsPlayerProfile PRIVATE BMS_PERF_DIAGNOSTICS)
target_link_libraries(BmsPlayerProfile PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)

add_executable(BmsRenderTests EXCLUDE_FROM_ALL tests/RenderTests.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_compile_definitions(BmsRenderTests PRIVATE BMS_PERF_DIAGNOSTICS)
target_link_libraries(BmsRenderTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)

add_executable(BmsCompatibilityTests tests/CompatibilityTests.cpp)
target_link_libraries(BmsCompatibilityTests PRIVATE bms_core gdiplus ole32 vfw32)
add_test(NAME BmsCompatibilityTests COMMAND BmsCompatibilityTests ${CMAKE_CURRENT_BINARY_DIR}/CompatibilitySamples)

add_executable(BmsCompatibilityPlaybackTests tests/CompatibilityPlaybackTests.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_link_libraries(BmsCompatibilityPlaybackTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh vfw32)
add_test(NAME BmsCompatibilityPlaybackTests COMMAND BmsCompatibilityPlaybackTests ${CMAKE_CURRENT_BINARY_DIR}/CompatibilitySamples)
set_tests_properties(BmsCompatibilityPlaybackTests PROPERTIES DEPENDS BmsCompatibilityTests RUN_SERIAL TRUE)

add_executable(BmsCoreTests tests/ParserTests.cpp)
target_link_libraries(BmsCoreTests PRIVATE bms_core)
add_test(NAME BmsCoreTests COMMAND BmsCoreTests)

add_executable(BmsLibraryTests tests/LibraryTests.cpp)
target_link_libraries(BmsLibraryTests PRIVATE bms_core bms_song_selection)
add_test(NAME BmsLibraryTests COMMAND BmsLibraryTests --test ${CMAKE_CURRENT_BINARY_DIR})

add_executable(BmsSongGroupingTests tests/SongGroupingTests.cpp)
target_link_libraries(BmsSongGroupingTests PRIVATE bms_core)
add_test(NAME BmsSongGroupingTests COMMAND BmsSongGroupingTests)

add_executable(BmsSongSelectionTests tests/SongSelectionTests.cpp)
target_link_libraries(BmsSongSelectionTests PRIVATE bms_song_selection)
add_test(NAME BmsSongSelectionTests COMMAND BmsSongSelectionTests)

add_executable(BmsAudioTests tests/AudioTests.cpp src/AudioEngine.cpp)
target_link_libraries(BmsAudioTests PRIVATE bms_core winmm mfplat mfreadwrite mfuuid ole32)
add_test(NAME BmsAudioTests COMMAND BmsAudioTests --effects ${CMAKE_SOURCE_DIR}/Assets/levelchange.mp3 ${CMAKE_SOURCE_DIR}/Assets/click.mp3)

add_executable(BmsSongCompatibilityTests EXCLUDE_FROM_ALL tests/SongCompatibilityTests.cpp src/AudioEngine.cpp)
target_link_libraries(BmsSongCompatibilityTests PRIVATE bms_core winmm mfplat mfreadwrite mfuuid ole32)

add_executable(BmsFrameTests tests/FrameTests.cpp)
target_include_directories(BmsFrameTests PRIVATE src)
target_link_libraries(BmsFrameTests PRIVATE winmm)
add_test(NAME BmsFrameTests COMMAND BmsFrameTests)

add_executable(BmsLatestEffectTests tests/LatestEffectTests.cpp)
target_include_directories(BmsLatestEffectTests PRIVATE src)
add_test(NAME BmsLatestEffectTests COMMAND BmsLatestEffectTests)

add_executable(BmsLongNoteTests tests/LongNoteTests.cpp)
target_include_directories(BmsLongNoteTests PRIVATE src)
add_test(NAME BmsLongNoteTests COMMAND BmsLongNoteTests)

add_executable(BmsJudgementTests tests/JudgementTests.cpp)
target_include_directories(BmsJudgementTests PRIVATE src)
add_test(NAME BmsJudgementTests COMMAND BmsJudgementTests)

add_executable(BmsFeedbackTests tests/FeedbackTests.cpp)
target_include_directories(BmsFeedbackTests PRIVATE src)
add_test(NAME BmsFeedbackTests COMMAND BmsFeedbackTests ${CMAKE_SOURCE_DIR}/Validation/splash-preview.bmp ${CMAKE_SOURCE_DIR}/Assets/hit-splash.png)

add_executable(BmsGrooveGaugeTests tests/GrooveGaugeTests.cpp)
target_include_directories(BmsGrooveGaugeTests PRIVATE src)
add_test(NAME BmsGrooveGaugeTests COMMAND BmsGrooveGaugeTests)

add_executable(BmsResultScoreTests tests/ResultScoreTests.cpp)
target_include_directories(BmsResultScoreTests PRIVATE src)
add_test(NAME BmsResultScoreTests COMMAND BmsResultScoreTests)

add_executable(BmsPracticeTests tests/PracticeTests.cpp)
target_include_directories(BmsPracticeTests PRIVATE src)
add_test(NAME BmsPracticeTests COMMAND BmsPracticeTests)

add_executable(BmsKeyBeamTests tests/KeyBeamTests.cpp)
target_include_directories(BmsKeyBeamTests PRIVATE src)
add_test(NAME BmsKeyBeamTests COMMAND BmsKeyBeamTests ${CMAKE_SOURCE_DIR}/Validation/key-beam-preview.bmp)

add_executable(BmsDisplayTests tests/DisplayTests.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_link_libraries(BmsDisplayTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
add_test(NAME BmsDisplayTests COMMAND BmsDisplayTests ${CMAKE_SOURCE_DIR}/Validation ${CMAKE_SOURCE_DIR}/Assets/turntable.png ${CMAKE_SOURCE_DIR}/Assets/hud-icon-slot.png ${CMAKE_SOURCE_DIR}/Assets/hit-splash.png)

# Phase 0 records existing native settings behavior; it does not enable a new UI.
add_executable(BmsSettingsPhase0Tests tests/SettingsPhase0Tests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
target_link_libraries(BmsSettingsPhase0Tests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
add_test(NAME BmsSettingsPhase0Tests COMMAND BmsSettingsPhase0Tests
    "${CMAKE_SOURCE_DIR}/tests/fixtures/SettingsPhase0.ini" "${CMAKE_BINARY_DIR}/Validation/SettingsPhase0")
set_tests_properties(BmsSettingsPhase0Tests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_executable(BmsSettingsControllerTests tests/SettingsControllerTests.cpp)
add_test(NAME BmsSettingsControllerTests COMMAND BmsSettingsControllerTests)

add_executable(BmsBgaTests tests/BgaTests.cpp)
target_link_libraries(BmsBgaTests PRIVATE bms_core gdiplus ole32)
add_test(NAME BmsBgaTests COMMAND BmsBgaTests ${CMAKE_SOURCE_DIR}/Validation/BgaTests)

add_executable(BmsVideoFrameScalerTests tests/VideoFrameScalerTests.cpp)
target_include_directories(BmsVideoFrameScalerTests PRIVATE src)
add_test(NAME BmsVideoFrameScalerTests COMMAND BmsVideoFrameScalerTests)

add_executable(BmsVideoTests tests/VideoBgaTests.cpp)
target_compile_definitions(BmsVideoTests PRIVATE BMS_VIDEO_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/video")
target_link_libraries(BmsVideoTests PRIVATE bms_core mfplat mfreadwrite mfuuid ole32 gdiplus vfw32)
add_test(NAME BmsVideoTests COMMAND BmsVideoTests ${CMAKE_CURRENT_BINARY_DIR}/Validation/VideoTests)

add_executable(BmsUiTests tests/UiTests.cpp)
target_link_libraries(BmsUiTests PRIVATE comctl32)
add_dependencies(BmsUiTests BmsPlayer)
add_test(NAME BmsUiTests COMMAND BmsUiTests $<TARGET_FILE:BmsPlayer> ${CMAKE_SOURCE_DIR}/Validation)

add_custom_command(TARGET BmsPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:BmsPlayer>/Assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/turntable.png $<TARGET_FILE_DIR:BmsPlayer>/Assets/turntable.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/blackbutton.png $<TARGET_FILE_DIR:BmsPlayer>/Assets/blackbutton.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/whitebutton.png $<TARGET_FILE_DIR:BmsPlayer>/Assets/whitebutton.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/hud-icon-slot.png $<TARGET_FILE_DIR:BmsPlayer>/Assets/hud-icon-slot.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/hit-splash.png $<TARGET_FILE_DIR:BmsPlayer>/Assets/hit-splash.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/levelchange.mp3 $<TARGET_FILE_DIR:BmsPlayer>/Assets/levelchange.mp3
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Assets/click.mp3 $<TARGET_FILE_DIR:BmsPlayer>/Assets/click.mp3
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/docs/licenses/Zlib.txt $<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses/Zlib.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/signalsmith-stretch/LICENSE.txt $<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses/Signalsmith-Stretch.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/signalsmith-stretch/signalsmith-linear/LICENSE.txt $<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses/Signalsmith-Linear.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/nvapi/License.txt $<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses/NVIDIA-NVAPI.txt)

add_executable(BmsPerformanceExperiment EXCLUDE_FROM_ALL tests/PerformanceExperiment.cpp)
target_link_libraries(BmsPerformanceExperiment PRIVATE bms_core)
add_executable(BmsGameplayProfile EXCLUDE_FROM_ALL tests/GameplayProfile.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_compile_definitions(BmsGameplayProfile PRIVATE BMS_PERF_DIAGNOSTICS BMS_FRAME_DIAGNOSTICS)
target_link_libraries(BmsGameplayProfile PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
add_dependencies(BmsGameplayProfile BmsPlayer)
add_executable(BmsOutputCadenceProfile EXCLUDE_FROM_ALL tests/OutputCadenceProfile.cpp)
target_include_directories(BmsOutputCadenceProfile PRIVATE src)
target_link_libraries(BmsOutputCadenceProfile PRIVATE d3d11 dxgi dcomp d2d1 winmm ole32)
add_executable(BmsFrameStudyRasterTests tests/FrameStudyRasterTests.cpp src/app.rc)
add_test(NAME BmsFrameStudyRasterTests COMMAND BmsFrameStudyRasterTests)

# Diagnostic executables use the same embedded gameplay font as the shipped game.
foreach(hud_font_target BmsDisplayTests BmsGameplayProfile BmsPlayerProfile BmsRenderTests)
    target_sources(${hud_font_target} PRIVATE src/app.rc)
endforeach()
set_property(SOURCE src/app.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/Michroma-Regular.ttf")

# Rust parser helpers remain opt-in. The song-selection model defaults to Rust
# outside Debug, and its C++ implementation remains available as a fallback.
include(cmake/RustBackend.cmake)

foreach(song_ui_target BmsPlayer BmsPlayerTest BmsPlayerProfile BmsDiagnosticsTests BmsRenderTests BmsCompatibilityPlaybackTests BmsDisplayTests BmsGameplayProfile BmsSettingsPhase0Tests)
    bms_attach_song_list_ui(${song_ui_target})
endforeach()

add_executable(SevenManiaSkinBindingTests tests/SkinBindingTests.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_compile_definitions(SevenManiaSkinBindingTests PRIVATE BMS_ENABLE_SKIN_BINDINGS)
target_link_libraries(SevenManiaSkinBindingTests PRIVATE sevenmania_skin_render bms_core bms_song_selection
    comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh d3d11 d2d1 windowscodecs)
bms_attach_song_list_ui(SevenManiaSkinBindingTests)
add_test(NAME SevenManiaSkinBindingTests COMMAND SevenManiaSkinBindingTests
    "${CMAKE_SOURCE_DIR}/tests/fixtures/skins/data-binding.semaskin" "${CMAKE_BINARY_DIR}/Validation/SkinBinding")
set_tests_properties(SevenManiaSkinBindingTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

add_executable(SevenManiaSkinApplicationTests tests/SkinApplicationTests.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_compile_definitions(SevenManiaSkinApplicationTests PRIVATE BMS_ENABLE_SKIN_BINDINGS)
target_link_libraries(SevenManiaSkinApplicationTests PRIVATE sevenmania_skin_render bms_core bms_song_selection
    comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh d3d11 d2d1 windowscodecs)
bms_attach_song_list_ui(SevenManiaSkinApplicationTests)
add_test(NAME SevenManiaSkinApplicationTests COMMAND SevenManiaSkinApplicationTests
    "${CMAKE_SOURCE_DIR}/tests/fixtures/skins" "${CMAKE_BINARY_DIR}/Validation/SkinApplication")
set_tests_properties(SevenManiaSkinApplicationTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

if(BMS_ENABLE_RMLUI AND BMS_ENABLE_RMLUI_SONG_LIST)
    add_executable(BmsRmlSongListTests tests/RmlSongListTests.cpp)
    target_link_libraries(BmsRmlSongListTests PRIVATE gdiplus)
    bms_attach_song_list_ui(BmsRmlSongListTests)
    add_test(NAME BmsRmlSongListTests COMMAND BmsRmlSongListTests "${CMAKE_BINARY_DIR}/Validation/RmlUi")

    add_executable(BmsRmlSongListHostTests tests/RmlSongListHostTests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
    target_link_libraries(BmsRmlSongListHostTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
    bms_attach_song_list_ui(BmsRmlSongListHostTests)
    add_test(NAME BmsRmlSongListHostTests COMMAND BmsRmlSongListHostTests "${CMAKE_BINARY_DIR}/Validation/RmlUi")
    set_tests_properties(BmsRmlSongListTests BmsRmlSongListHostTests PROPERTIES TIMEOUT 30)
endif()

if(BMS_ENABLE_RMLUI AND BMS_ENABLE_RMLUI_SETTINGS)
    add_executable(BmsSongLibraryUiTests tests/SongLibraryUiTests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
    target_link_libraries(BmsSongLibraryUiTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
    bms_attach_song_list_ui(BmsSongLibraryUiTests)
    add_test(NAME BmsSongLibraryUiTests COMMAND BmsSongLibraryUiTests "${CMAKE_BINARY_DIR}/Validation/SongLibrary2UI")
    set_tests_properties(BmsSongLibraryUiTests PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_executable(BmsSettingsPhase3Tests tests/SettingsPhase3Tests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
    target_link_libraries(BmsSettingsPhase3Tests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
    bms_attach_song_list_ui(BmsSettingsPhase3Tests)
    add_test(NAME BmsSettingsPhase3Tests COMMAND BmsSettingsPhase3Tests "${CMAKE_SOURCE_DIR}/tests/fixtures/SettingsPhase0.ini" "${CMAKE_BINARY_DIR}/Validation/SettingsPhase3")
    set_tests_properties(BmsSettingsPhase3Tests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_executable(BmsSettingsPhase5Tests tests/SettingsPhase5Tests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
    target_link_libraries(BmsSettingsPhase5Tests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
    bms_attach_song_list_ui(BmsSettingsPhase5Tests)
    add_test(NAME BmsSettingsPhase5Tests COMMAND BmsSettingsPhase5Tests "${CMAKE_SOURCE_DIR}/tests/fixtures/SettingsPhase0.ini" "${CMAKE_BINARY_DIR}/Validation/SettingsPhase5")
    set_tests_properties(BmsSettingsPhase5Tests PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_executable(BmsSettingsPhase6Tests tests/SettingsPhase6Tests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
    target_link_libraries(BmsSettingsPhase6Tests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
    bms_attach_song_list_ui(BmsSettingsPhase6Tests)
    add_test(NAME BmsSettingsPhase6Tests COMMAND BmsSettingsPhase6Tests "${CMAKE_SOURCE_DIR}/tests/fixtures/SettingsPhase0.ini" "${CMAKE_BINARY_DIR}/Validation/SettingsPhase6")
    set_tests_properties(BmsSettingsPhase6Tests PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    add_executable(BmsRmlSettingsHostTests tests/RmlSettingsHostTests.cpp)
    target_link_libraries(BmsRmlSettingsHostTests PRIVATE gdiplus)
    bms_attach_song_list_ui(BmsRmlSettingsHostTests)
    add_test(NAME BmsRmlSettingsHostTests COMMAND BmsRmlSettingsHostTests
        "$<TARGET_FILE_DIR:BmsRmlSettingsHostTests>/Assets/UI/settings-probe.rml" "${CMAKE_BINARY_DIR}/Validation/RmlSettings")
    set_tests_properties(BmsRmlSettingsHostTests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()

bms_finalize_performance()

add_executable(BmsAccuracyRestartTests tests/AccuracyRestartTests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
target_link_libraries(BmsAccuracyRestartTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
bms_attach_song_list_ui(BmsAccuracyRestartTests)
add_test(NAME BmsAccuracyRestartTests COMMAND BmsAccuracyRestartTests "${CMAKE_BINARY_DIR}/Validation/AccuracyRestart")
set_tests_properties(BmsAccuracyRestartTests PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

add_executable(BmsControllerTests tests/ControllerTests.cpp)
target_link_libraries(BmsControllerTests PRIVATE ole32 comctl32)
add_test(NAME BmsControllerTests COMMAND BmsControllerTests "${CMAKE_BINARY_DIR}/Validation/Controller")
set_tests_properties(BmsControllerTests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

add_executable(SevenManiaSkinEditor WIN32 src/editor/SkinEditor.cpp)
add_custom_command(TARGET SevenManiaSkinEditor POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SevenManiaSkinEditor>/Assets/Fonts"
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts" "$<TARGET_FILE_DIR:SevenManiaSkinEditor>/Assets/Fonts")
set_target_properties(SevenManiaSkinEditor PROPERTIES OUTPUT_NAME "SevenMania Skin Editor")
target_link_libraries(SevenManiaSkinEditor PRIVATE sevenmania_skin_render comdlg32 comctl32 d2d1 ole32)
target_link_libraries(SevenManiaSkinEditor PRIVATE shell32)
add_test(NAME SevenManiaSkinEditorSmoke COMMAND SevenManiaSkinEditor --smoke)
set_tests_properties(SevenManiaSkinEditorSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 30 RUN_SERIAL TRUE)
add_executable(SevenManiaSkinAnimationTests tests/SkinAnimationTests.cpp)
target_link_libraries(SevenManiaSkinAnimationTests PRIVATE sevenmania_skin)
add_test(NAME SevenManiaSkinAnimationTests COMMAND SevenManiaSkinAnimationTests)
foreach(default_skin_target BmsPlayer SevenManiaSkinApplicationTests SevenManiaSkinEditor)
 # Run even when the executable is already up to date: skin-only edits must ship.
 add_custom_target(${default_skin_target}DefaultSkinAssets
 COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${default_skin_target}>/Assets/Skins"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Assets/Skins/DefaultReference.semaskin" "$<TARGET_FILE_DIR:${default_skin_target}>/Assets/Skins/DefaultReference.semaskin"
 DEPENDS "${CMAKE_SOURCE_DIR}/Assets/Skins/DefaultReference.semaskin"
 VERBATIM)
 add_dependencies(${default_skin_target} ${default_skin_target}DefaultSkinAssets)
endforeach()

add_custom_command(TARGET BmsPlayer POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/pl_mpeg/NOTICE.txt" "$<TARGET_FILE_DIR:BmsPlayer>/Assets/Licenses/PL_MPEG.txt")
add_custom_command(TARGET BmsPlayer POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Assets/hud-hi-speed.png" "${CMAKE_SOURCE_DIR}/Assets/hud-gauge-mode.png" "$<TARGET_FILE_DIR:BmsPlayer>/Assets")

# Every standalone skin renderer uses the same built-in font faces as the game.
foreach(font_target SevenManiaSkinRenderTests SevenManiaSkinBindingTests SevenManiaSkinApplicationTests SevenManiaSkinEditor)
 target_sources(${font_target} PRIVATE src/app.rc)
endforeach()
set_property(SOURCE src/app.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/Oxanium-Regular.ttf" "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/Oxanium-Bold.ttf")
# Opt-in real-application memory transition diagnostic. Keep settings/assets isolated.
add_executable(BmsMemoryTransitionTests EXCLUDE_FROM_ALL tests/MemoryTransitionTests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
set_target_properties(BmsMemoryTransitionTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MemoryTransition")
target_compile_definitions(BmsMemoryTransitionTests PRIVATE BMS_ENABLE_SKIN_BINDINGS)
target_link_libraries(BmsMemoryTransitionTests PRIVATE bms_core bms_song_selection sevenmania_skin_render comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
bms_attach_song_list_ui(BmsMemoryTransitionTests)
add_custom_command(TARGET BmsMemoryTransitionTests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:BmsMemoryTransitionTests>/Assets")

add_executable(SevenManiaSkinHudImageTests tests/SkinHudImageTests.cpp src/app.rc)
target_link_libraries(SevenManiaSkinHudImageTests PRIVATE sevenmania_skin_render d3d11 d2d1 windowscodecs ole32 comdlg32 psapi)
add_test(NAME SevenManiaSkinHudImageTests COMMAND SevenManiaSkinHudImageTests "${CMAKE_SOURCE_DIR}/tests/fixtures/skins/hud-images.semaskin" "${CMAKE_BINARY_DIR}/Validation/SkinHudImages")
set_tests_properties(SevenManiaSkinHudImageTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

add_executable(SevenManiaDefaultHudImageTests tests/DefaultHudImageTests.cpp src/app.rc)
target_link_libraries(SevenManiaDefaultHudImageTests PRIVATE sevenmania_skin_render d3d11 d2d1 windowscodecs ole32 comdlg32)
add_test(NAME SevenManiaDefaultHudImageTests COMMAND SevenManiaDefaultHudImageTests "${CMAKE_SOURCE_DIR}/Assets/Skins/DefaultReference.semaskin" "${CMAKE_BINARY_DIR}/Validation/DefaultHudImages")
set_tests_properties(SevenManiaDefaultHudImageTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Isolated diagnostic build: probes never run in the default player.
add_library(sevenmania_skin_render_hud_probe STATIC EXCLUDE_FROM_ALL src/skin/SkinRenderer.cpp src/skin/SkinApplication.cpp)
target_compile_definitions(sevenmania_skin_render_hud_probe PRIVATE BMS_HUD_FRAME_PROBE)
target_link_libraries(sevenmania_skin_render_hud_probe PUBLIC sevenmania_skin PRIVATE sevenmania_skin_inflate d3d11 d2d1 msimg32 windowscodecs ole32 gdi32 user32)
add_executable(BmsHudFrameProfile EXCLUDE_FROM_ALL tests/HudFrameProfile.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
set_target_properties(BmsHudFrameProfile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/HudProbe")
target_compile_definitions(BmsHudFrameProfile PRIVATE BMS_ENABLE_SKIN_BINDINGS BMS_HUD_FRAME_PROBE)
target_link_libraries(BmsHudFrameProfile PRIVATE sevenmania_skin_render_hud_probe bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
bms_attach_song_list_ui(BmsHudFrameProfile)
add_custom_command(TARGET BmsHudFrameProfile POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:BmsHudFrameProfile>/Assets")

add_custom_command(TARGET BmsPlayer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/Assets/hud-fast.png" "${CMAKE_SOURCE_DIR}/Assets/hud-slow.png" "$<TARGET_FILE_DIR:BmsPlayer>/Assets")

# Launcher shares the existing native engine's configuration and metadata cache.
add_executable(SevenManiaLauncher WIN32 src/Launcher.cpp src/app.rc)
target_link_libraries(SevenManiaLauncher PRIVATE bms_core comctl32 shell32 ole32)
add_custom_command(TARGET BmsPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:BmsPlayer> $<TARGET_FILE_DIR:BmsPlayer>/SevenMania.exe)
add_executable(SevenManiaLauncherTests tests/LauncherTests.cpp)
target_link_libraries(SevenManiaLauncherTests PRIVATE bms_core)
add_test(NAME SevenManiaLauncherTests COMMAND SevenManiaLauncherTests "${CMAKE_BINARY_DIR}/Validation/Launcher")

add_executable(SevenManiaLauncherUiTests tests/LauncherUiTests.cpp)
target_link_libraries(SevenManiaLauncherUiTests PRIVATE bms_core comctl32 shell32 ole32 gdiplus)
add_dependencies(SevenManiaLauncherUiTests BmsPlayer)
add_test(NAME SevenManiaLauncherUiTests COMMAND SevenManiaLauncherUiTests "${CMAKE_BINARY_DIR}/Validation/LauncherUI" "$<TARGET_FILE_DIR:BmsPlayer>")
set_tests_properties(SevenManiaLauncherUiTests PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_executable(SevenManiaStartupIntegrationTests tests/StartupSettingsIntegrationTests.cpp src/AudioEngine.cpp src/SystemResources.cpp src/app.rc)
target_link_libraries(SevenManiaStartupIntegrationTests PRIVATE bms_core bms_song_selection comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh)
bms_attach_song_list_ui(SevenManiaStartupIntegrationTests)
add_test(NAME SevenManiaStartupIntegrationTests COMMAND SevenManiaStartupIntegrationTests "${CMAKE_BINARY_DIR}/Validation/LauncherEngine")

# Optional user-provided navigation sound; missing assets never fail the build.
add_custom_command(TARGET BmsPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_SOURCE_DIR}/Assets/songlistscroll.mp3" "-DDEST=$<TARGET_FILE_DIR:BmsPlayer>/Assets/songlistscroll.mp3" -P "${CMAKE_SOURCE_DIR}/cmake/CopyOptionalAsset.cmake")

add_executable(SevenManiaOfficialJudgementTests tests/OfficialJudgementTests.cpp)
target_include_directories(SevenManiaOfficialJudgementTests PRIVATE src)
add_test(NAME SevenManiaOfficialJudgementTests COMMAND SevenManiaOfficialJudgementTests)
add_executable(SevenManiaOfficialJudgementIntegrationTests tests/OfficialJudgementIntegrationTests.cpp src/AudioEngine.cpp src/SystemResources.cpp)
target_compile_definitions(SevenManiaOfficialJudgementIntegrationTests PRIVATE BMS_ENABLE_SKIN_BINDINGS)
target_link_libraries(SevenManiaOfficialJudgementIntegrationTests PRIVATE sevenmania_skin_render bms_core bms_song_selection
    comctl32 comdlg32 shell32 shlwapi winmm mfplat mfreadwrite mfuuid ole32 gdiplus psapi pdh d3d11 d2d1 windowscodecs)
bms_attach_song_list_ui(SevenManiaOfficialJudgementIntegrationTests)
add_test(NAME SevenManiaOfficialJudgementIntegrationTests COMMAND SevenManiaOfficialJudgementIntegrationTests "${CMAKE_BINARY_DIR}/Validation/OfficialJudge")
set_tests_properties(SevenManiaOfficialJudgementIntegrationTests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
