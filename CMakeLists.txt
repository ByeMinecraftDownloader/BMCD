cmake_minimum_required(VERSION 3.21)

project(ByeMinecraftDownloader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml Network Concurrent Sql Svg Test)

# Optional neural embeddings (0.7). Without onnxruntime the index still works
# with the deterministic hashing embedder and FTS5.
option(BMCD_ENABLE_RAG "Build the knowledge index (rag/)" ON)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    PATHS /usr/include/onnxruntime /usr/local/include/onnxruntime)
find_library(ONNXRUNTIME_LIBRARY onnxruntime)
if(BMCD_ENABLE_RAG AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    set(BMCD_HAVE_ONNXRUNTIME TRUE)
    message(STATUS "RAG: onnxruntime found at ${ONNXRUNTIME_LIBRARY}")
else()
    set(BMCD_HAVE_ONNXRUNTIME FALSE)
    message(STATUS "RAG: onnxruntime not found, using hashing embedder only")
endif()
# Windows has no system zlib and Qt does not expose its bundled copy, so fall
# back to a pinned source build instead of making every Windows user install
# vcpkg first. Linux/macOS keep using their system zlib.
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    include(FetchContent)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(zlib)
    if(TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib)
        add_library(ZLIB::ZLIB ALIAS zlib)
    else()
        message(FATAL_ERROR "zlib was fetched but exposed no library target")
    endif()
    message(STATUS "zlib: using bundled v1.3.1")
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(bmcd
    src/main.cpp
)
target_compile_definitions(bmcd PRIVATE BMCD_VERSION="${PROJECT_VERSION}")

# Keep the generated QML module out of the build root: a "<target>/" directory
# there would collide with the executable of the same name.
set_source_files_properties(qml2/Motion.qml qml2/Tokens.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(bmcd_ui STATIC)
qt_add_qml_module(bmcd_ui
    URI BMCD.UI
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    NO_PLUGIN
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml-ui
    QML_FILES
        qml2/Main.qml
        qml2/Motion.qml
        qml2/Tokens.qml
        qml2/ui/Icon.qml
        qml2/ui/NavRail.qml
        qml2/ui/ModPicker.qml
        qml2/ui/PageHeader.qml
        qml2/ui/SectionLabel.qml
        qml2/ui/SettingRow.qml
        qml2/ui/WindowChrome.qml
        qml2/ui/WindowResizeHandles.qml
        qml2/ui/controls/AppButton.qml
        qml2/ui/controls/CircleProgress.qml
        qml2/ui/controls/ConfirmDialog.qml
        qml2/ui/controls/Panel.qml
        qml2/ui/controls/ProgressBar.qml
        qml2/ui/controls/RippleCanvas.qml
        qml2/ui/controls/SearchField.qml
        qml2/ui/controls/ToastBar.qml
        qml2/views/OrderView.qml
        qml2/views/CookView.qml
        qml2/views/ServeView.qml
        qml2/views/SettingsView.qml
)
target_link_libraries(bmcd_ui PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

# UI translations. Chinese is the source language; the compiled English catalog
# is committed so every platform ships it without needing Qt Linguist at build
# time. Refresh it with lrelease after editing i18n/bmcd_en.ts.
qt_add_resources(bmcd "modaliases"
    PREFIX "/data"
    BASE data
    FILES data/mod-aliases.json
)

qt_add_resources(bmcd "i18n"
    PREFIX "/i18n"
    BASE i18n
    FILES i18n/bmcd_en.qm
)

qt_add_qml_module(bmcd
    URI BMCD
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml
    SOURCES
        src/theme/Palette.h
        src/theme/Palette.cpp
        src/theme/ColorMath.h
        src/theme/ColorMath.cpp
        src/theme/TokenBuilder.h
        src/theme/TokenBuilder.cpp
        src/theme/IconImageProvider.h
        src/theme/IconImageProvider.cpp
        src/theme/ThemeSource.h
        src/theme/ThemeSource.cpp
        src/platform/Paths.h
        src/platform/Paths.cpp
        src/platform/FileOps.h
        src/platform/FileOps.cpp
        src/platform/PlatformInfo.h
        src/platform/PlatformInfo.cpp
        src/platform/Console.h
        src/platform/Console.cpp
        src/platform/Process.h
        src/core/VersionListModel.h
        src/core/VersionListModel.cpp
        src/core/VersionFilterModel.h
        src/core/VersionFilterModel.cpp
        src/core/VersionService.h
        src/core/VersionService.cpp
        src/core/Plan.h
        src/core/PlanParser.h
        src/core/PlanParser.cpp
        src/core/DownloadTask.h
        src/core/DownloadTask.cpp
        src/core/DownloadSources.h
        src/core/DownloadSources.cpp
        src/core/VersionDetail.h
        src/core/VersionDetail.cpp
        src/core/AppSettings.h
        src/core/AppSettings.cpp
        src/core/JavaLocator.h
        src/core/JavaLocator.cpp
        src/core/ProfileMerger.h
        src/core/ProfileMerger.cpp
        src/core/LoaderInstaller.h
        src/core/LoaderInstaller.cpp
        src/core/CookSession.h
        src/core/CookSession.cpp
        src/core/InstalledVersions.h
        src/core/InstalledVersions.cpp
        src/core/Pack.h
        src/core/Pack.cpp
        src/core/ZipWriter.h
        src/core/ZipWriter.cpp
        src/core/ZipReader.h
        src/core/ZipReader.cpp
        src/core/PackImporter.h
        src/core/PackImporter.cpp
        src/core/Instances.h
        src/core/Instances.cpp
        src/core/LicenseGate.h
        src/core/LicenseGate.cpp
        src/core/Renderers.h
        src/core/Renderers.cpp
        src/core/Deployer.h
        src/core/Deployer.cpp
        src/rag/Embedder.h
        src/rag/Embedder.cpp
        src/rag/Tokenizer.h
        src/rag/Tokenizer.cpp
        src/rag/Index.h
        src/rag/Index.cpp
        src/rag/Retriever.h
        src/rag/Retriever.cpp
        src/rag/Corpus.h
        src/rag/Corpus.cpp
        src/rag/Eval.h
        src/rag/Eval.cpp
        src/rag/RagController.h
        src/rag/RagController.cpp
        src/core/PackController.h
        src/core/PackController.cpp
        src/mods/ModResolver.h
        src/mods/ModResolver.cpp
        src/mods/ModAliases.cpp
        src/mods/ModrinthApi.h
        src/mods/ModrinthApi.cpp
        src/mods/ModsController.h
        src/mods/ModsController.cpp
    RESOURCE_PREFIX /qt/qml
)

# The generated type registration includes every header by its bare name, so
# each directory holding a QML-exposed type has to be on the include path.
target_include_directories(bmcd PRIVATE
    src
    src/theme
    src/platform
    src/rag
    src/core
    src/mods
)

target_link_libraries(bmcd PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Svg
    bmcd_ui
    ZLIB::ZLIB
)

if(BMCD_HAVE_ONNXRUNTIME)
    target_include_directories(bmcd PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_compile_definitions(bmcd PRIVATE BMCD_HAVE_ONNXRUNTIME=1)
    target_link_libraries(bmcd PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()

qt_import_qml_plugins(bmcd)

# A windowed build on Windows has no console; platform::attachConsole() is what
# keeps `bmcd --plan X` usable from cmd.exe.
if(WIN32)
    set_target_properties(bmcd PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(bmcd PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "BMCD"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.bmcd.BMCD"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

install(TARGETS bmcd
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)

# Desktop entry, icon and AppStream metadata: needed by Flatpak and by any
# distro package. All three use the app id (org.bmcd.BMCD) so that the window's
# DesktopFileName, the icon lookup and the AppStream component line up.
install(FILES packaging/bmcd.desktop
    DESTINATION share/applications
    RENAME org.bmcd.BMCD.desktop
)
install(FILES packaging/bmcd.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME org.bmcd.BMCD.svg
)
install(FILES packaging/flatpak/org.bmcd.BMCD.metainfo.xml
    DESTINATION share/metainfo
)
install(FILES LICENSE
    DESTINATION share/licenses/bmcd
)

# --- tests ------------------------------------------------------------------
# Unit tests are plain executables (not qt_add_test) so they build with the
# same compiler flags as the app and can be run without a display.
enable_testing()

add_executable(tst_platform
    tests/tst_platform.cpp
    src/platform/Paths.cpp
    src/platform/FileOps.cpp
    src/platform/PlatformInfo.cpp
    src/platform/Console.cpp
)
target_include_directories(tst_platform PRIVATE src src/platform)
target_link_libraries(tst_platform PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME platform COMMAND tst_platform)

add_executable(tst_theme
    tests/tst_theme.cpp
    src/theme/ColorMath.cpp
    src/theme/TokenBuilder.cpp
    src/theme/ThemeSource.cpp
    src/platform/PlatformInfo.cpp
)
target_include_directories(tst_theme PRIVATE src src/theme src/platform)
target_link_libraries(tst_theme PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME theme COMMAND tst_theme)

add_executable(tst_pack
    tests/tst_pack.cpp
    src/core/Pack.cpp
    src/core/PlanParser.cpp
    src/core/ZipWriter.cpp
    src/core/ZipReader.cpp
    src/core/PackImporter.cpp
    src/core/Instances.cpp
    src/core/LicenseGate.cpp
    src/core/Renderers.cpp
    src/core/Deployer.cpp
    src/platform/FileOps.cpp
    src/platform/Paths.cpp
)
target_include_directories(tst_pack PRIVATE src src/core src/platform)
target_link_libraries(tst_pack PRIVATE Qt6::Core Qt6::Network Qt6::Test ZLIB::ZLIB)
add_test(NAME pack COMMAND tst_pack)

add_executable(tst_mods
    tests/tst_mods.cpp
    src/mods/ModResolver.cpp
    src/mods/ModAliases.cpp
)
target_include_directories(tst_mods PRIVATE src src/mods)
target_link_libraries(tst_mods PRIVATE Qt6::Core Qt6::Test)
add_test(NAME mods COMMAND tst_mods)

add_executable(tst_javalocator
    tests/tst_javalocator.cpp
    src/core/JavaLocator.cpp
    src/core/AppSettings.cpp
    src/platform/FileOps.cpp
    src/platform/Paths.cpp
    src/platform/PlatformInfo.cpp
    src/platform/Console.cpp
)
target_include_directories(tst_javalocator PRIVATE src)
target_link_libraries(tst_javalocator PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME javalocator COMMAND tst_javalocator)

add_executable(tst_profilemerger
    tests/tst_profilemerger.cpp
    src/core/ProfileMerger.cpp
    src/platform/FileOps.cpp
    src/platform/Paths.cpp
)
target_include_directories(tst_profilemerger PRIVATE src)
target_link_libraries(tst_profilemerger PRIVATE Qt6::Core Qt6::Test)
add_test(NAME profilemerger COMMAND tst_profilemerger)

add_executable(tst_loaderinstaller
    tests/tst_loaderinstaller.cpp
    src/core/LoaderInstaller.cpp
    src/core/ProfileMerger.cpp
    src/core/AppSettings.cpp
    src/platform/FileOps.cpp
    src/platform/Paths.cpp
    src/platform/PlatformInfo.cpp
    src/platform/Console.cpp
)
target_include_directories(tst_loaderinstaller PRIVATE src)
target_link_libraries(tst_loaderinstaller PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME loaderinstaller COMMAND tst_loaderinstaller)

add_executable(tst_download
    tests/tst_download.cpp
    src/core/DownloadTask.cpp
    src/core/DownloadSources.cpp
    src/core/AppSettings.cpp
    src/platform/FileOps.cpp
    src/platform/Paths.cpp
    src/platform/PlatformInfo.cpp
    src/platform/Console.cpp
)
target_include_directories(tst_download PRIVATE src)
target_link_libraries(tst_download PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME download COMMAND tst_download)

add_executable(tst_packcontroller
    tests/tst_packcontroller.cpp
    src/core/PackController.cpp
    src/core/DownloadTask.cpp
    src/core/DownloadSources.cpp
    src/core/AppSettings.cpp
    src/core/Pack.cpp
    src/core/PlanParser.cpp
    src/core/ZipWriter.cpp
    src/core/ZipReader.cpp
    src/core/PackImporter.cpp
    src/core/Instances.cpp
    src/core/LicenseGate.cpp
    src/core/Renderers.cpp
    src/core/Deployer.cpp
    src/platform/FileOps.cpp
    src/platform/Paths.cpp
    src/platform/PlatformInfo.cpp
    src/platform/Console.cpp
)
target_include_directories(tst_packcontroller PRIVATE src src/core src/platform)
target_link_libraries(tst_packcontroller PRIVATE
    Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Qml Qt6::Test ZLIB::ZLIB)
add_test(NAME packcontroller COMMAND tst_packcontroller)

add_executable(tst_rag
    tests/tst_rag.cpp
    src/rag/Tokenizer.cpp
    src/rag/Embedder.cpp
    src/rag/Index.cpp
    src/rag/Retriever.cpp
    src/rag/Corpus.cpp
    src/rag/Eval.cpp
    src/rag/RagController.cpp
    src/platform/Console.cpp
    src/platform/Paths.cpp
)
target_include_directories(tst_rag PRIVATE src src/rag src/platform)
target_compile_definitions(tst_rag PRIVATE
    BMCD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(tst_rag PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Test)
if(BMCD_HAVE_ONNXRUNTIME)
    target_include_directories(tst_rag PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_compile_definitions(tst_rag PRIVATE BMCD_HAVE_ONNXRUNTIME=1)
    target_link_libraries(tst_rag PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()
add_test(NAME rag COMMAND tst_rag)

add_executable(ui_check tests/ui_check.cpp)
target_link_libraries(ui_check PRIVATE Qt6::Gui)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Pixel-level regression for the new UI. Offscreen plugin is present on the
    # build machine; on other platforms the runner may not ship it.
    add_test(NAME ui_golden
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_golden.sh
                $<TARGET_FILE:bmcd> $<TARGET_FILE:ui_check>)
    set_tests_properties(ui_golden PROPERTIES TIMEOUT 90)
endif()
