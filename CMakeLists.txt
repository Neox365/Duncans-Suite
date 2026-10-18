cmake_minimum_required(VERSION 3.20)
project(DuncansDriverUpdater VERSION 1.1.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fester Ausgabeordner: release/ (der Installer liest genau von dort)
set(OUT_DIR ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR})
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${OUT_DIR})
endforeach()

add_executable(DuncansDriverUpdater WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/HardwareScanner.cpp
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
    target_link_options(DuncansDriverUpdater PRIVATE -municode -mwindows -static -s)
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
    target_link_options(DuncansSuite PRIVATE -municode -mwindows -static -s)
endif()
target_link_libraries(DuncansSuite PRIVATE advapi32 user32 gdi32 shell32 shlwapi ole32 dwmapi gdiplus wininet bcrypt)

# ---- Duncan's PDF: nativer PDF-Betrachter/-Editor (PDFium wird zur Laufzeit aus pdfium.dll geladen) ----
add_executable(DuncansPdfReader WIN32
    pdf/main.cpp
    pdf/PdfDocument.cpp
    resources/pdf.rc
)
set_target_properties(DuncansPdfReader PROPERTIES OUTPUT_NAME DuncansPdf)
target_compile_definitions(DuncansPdfReader PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(DuncansPdfReader PRIVATE /utf-8 /W4 /permissive-)
    set_property(TARGET DuncansPdfReader PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(DuncansPdfReader PRIVATE /MANIFEST:NO)
else()
    target_compile_options(DuncansPdfReader PRIVATE -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type)
    target_link_options(DuncansPdfReader PRIVATE -municode -mwindows -static -s)
endif()
target_link_libraries(DuncansPdfReader PRIVATE user32 gdi32 shell32 ole32 dwmapi gdiplus comdlg32 uxtheme)

# pdfium.dll (pdf/third_party/get_pdfium.ps1) neben die EXEs in release/ kopieren, damit der PDF Reader und der Installer sie finden
if(EXISTS "${CMAKE_SOURCE_DIR}/pdf/third_party/pdfium.dll")
    add_custom_target(copy_pdfium ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/pdf/third_party/pdfium.dll" "${OUT_DIR}/pdfium.dll"
        COMMENT "pdfium.dll nach release/ kopieren")
endif()
