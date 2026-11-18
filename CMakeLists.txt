cmake_minimum_required(VERSION 3.21)

project(EIS-Extractor
    VERSION 0.1.0
    DESCRIPTION "E.I.S CYBERCORE archive operations manager"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

# ---------------------------------------------------------------------------
# Core library: archive engine, security, queue, settings, logging.
# Qt Widgets is deliberately not linked here so tests run headless.
# ---------------------------------------------------------------------------
add_library(eis_core STATIC
    src/core/Types.h
    src/core/Utils.h
    src/core/Utils.cpp
    src/logging/Logger.h
    src/logging/Logger.cpp
    src/settings/AppSettings.h
    src/settings/AppSettings.cpp
    src/security/PathGuard.h
    src/security/PathGuard.cpp
    src/archive/ArchiveTypes.h
    src/archive/ArchiveService.h
    src/archive/ArchiveService.cpp
    src/backends/Backend.h
    src/backends/BackendRegistry.h
    src/backends/BackendRegistry.cpp
    src/backends/LibarchiveBackend.h
    src/backends/LibarchiveBackend.cpp
    src/backends/SevenZipBackend.h
    src/backends/SevenZipBackend.cpp
    src/extraction/ExtractionTask.h
    src/extraction/ExtractionTask.cpp
    src/integrity/IntegrityChecker.h
    src/integrity/IntegrityChecker.cpp
    src/queue/JobQueue.h
    src/queue/JobQueue.cpp
)
target_include_directories(eis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(eis_core
    PUBLIC Qt6::Core PkgConfig::LIBARCHIVE
)
target_compile_options(eis_core PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
qt_add_executable(eis-extractor
    src/app/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/OverviewPage.h
    src/ui/OverviewPage.cpp
    src/ui/OperationsPage.h
    src/ui/OperationsPage.cpp
    src/ui/ProgressWindow.h
    src/ui/ProgressWindow.cpp
    src/ui/InspectorPage.h
    src/ui/InspectorPage.cpp
    src/ui/IntegrityPage.h
    src/ui/IntegrityPage.cpp
    src/ui/LogPage.h
    src/ui/LogPage.cpp
    src/ui/SettingsPage.h
    src/ui/SettingsPage.cpp
    src/ui/DestinationDialog.h
    src/ui/DestinationDialog.cpp
    src/widgets/HudWidgets.h
    src/widgets/HudWidgets.cpp
    src/widgets/ArchiveTreeModel.h
    src/widgets/ArchiveTreeModel.cpp
    resources/resources.qrc
)
target_link_libraries(eis-extractor PRIVATE eis_core Qt6::Widgets Qt6::Gui)
target_compile_options(eis-extractor PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS eis-extractor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS packaging/eis-extractor-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/eis-extractor.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/eis-extractor.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES packaging/servicemenus/eis-extractor-extract.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
