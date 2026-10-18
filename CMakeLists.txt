cmake_minimum_required(VERSION 3.20)
project(DuncansDriverUpdater VERSION 1.0.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fester Ausgabeordner: Projekt-Hauptordner (der Installer liest genau von dort)
set(OUT_DIR ${CMAKE_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR})
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${OUT_DIR})
endforeach()

add_executable(DuncansDriverUpdater WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/HardwareScanner.cpp
    src/DriverInfo.cpp
    src/Icons.cpp
    src/UpdateChecker.cpp
    src/UpdateInstaller.cpp
    src/VendorReleases.cpp
    src/Utils.cpp
    resources/app.rc
)

target_compile_definitions(DuncansDriverUpdater PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

if(MSVC)
    # Quellen sind UTF-8; statische CRT, damit keine VC++-Redistributable noetig ist
    target_compile_options(DuncansDriverUpdater PRIVATE /utf-8 /W4 /permissive-)
    set_property(TARGET DuncansDriverUpdater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Manifest kommt aus resources/app.rc (UAC: requireAdministrator) - kein automatisches Manifest erzeugen
    target_link_options(DuncansDriverUpdater PRIVATE /MANIFEST:NO)
else()
    # MinGW-w64
    target_compile_options(DuncansDriverUpdater PRIVATE -Wall -Wextra)
    target_link_options(DuncansDriverUpdater PRIVATE -municode -mwindows -static)
endif()

# Benoetigte Windows-Bibliotheken
target_link_libraries(DuncansDriverUpdater PRIVATE setupapi cfgmgr32 user32 gdi32 dwmapi advapi32 shell32 ole32 oleaut32 gdiplus comdlg32 msimg32)

# Default-Hersteller-Tabelle (resources/vendor_releases.json) bei jedem Build neben die EXE kopieren.
# Dort liest sie die App; der Installer packt genau diese Kopie. Aenderungen daher in resources/ machen.
add_custom_target(copy_vendor_releases ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/vendor_releases.json"
            "$<TARGET_FILE_DIR:DuncansDriverUpdater>/vendor_releases.json"
    COMMENT "vendor_releases.json neben die EXE kopieren")
add_dependencies(DuncansDriverUpdater copy_vendor_releases)

# ---- Duncan's Suite: zentraler Launcher fuer Tidy + Driver Updater ----
add_executable(DuncansSuite WIN32
    suite/main.cpp
    src/Icons.cpp
    resources/suite.rc
)
target_compile_definitions(DuncansSuite PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
                           SUITE_VERSION=L\"${PROJECT_VERSION}\")
if(MSVC)
    target_compile_options(DuncansSuite PRIVATE /utf-8 /W4 /permissive-)
    set_property(TARGET DuncansSuite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(DuncansSuite PRIVATE /MANIFEST:NO)
else()
    target_compile_options(DuncansSuite PRIVATE -Wall -Wextra)
    target_link_options(DuncansSuite PRIVATE -municode -mwindows -static)
endif()
target_link_libraries(DuncansSuite PRIVATE user32 gdi32 shell32 shlwapi ole32 dwmapi gdiplus)
