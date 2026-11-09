# Obex.exe, its core library and the tests. Use build.ps1, which also fetches
# the build tools and builds the installer.
cmake_minimum_required(VERSION 3.21)
project(Obex CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /WX /permissive- /sdl /utf-8 /guard:cf /EHsc /external:anglebrackets /external:W0)
add_link_options(/guard:cf /CETCOMPAT /OPT:REF /OPT:ICF)

add_library(obexcore STATIC
    src/core/clipboard.cpp
    src/core/crypto.cpp
    src/core/csv.cpp
    src/core/generator.cpp
    src/core/hello.cpp
    src/core/hotkey.cpp
    src/core/paths.cpp
    src/core/settings.cpp
    src/core/text.cpp
    src/core/vault.cpp
)
target_include_directories(obexcore PUBLIC src)
target_link_libraries(obexcore PUBLIC user32 advapi32 shell32 ole32 bcrypt crypt32 windowsapp)

add_executable(Obex WIN32
    src/app/generator_panel.cpp
    src/app/hash_panel.cpp
    src/app/main.cpp
    src/app/main_window.cpp
    src/app/panel.cpp
    src/app/password_panel.cpp
    src/app/quick_panel.cpp
    src/app/recovery_panel.cpp
    src/app/settings_panel.cpp
    src/app/theme.cpp
    src/app/tray_app.cpp
    src/app/vault_panel.cpp
    src/app/Obex.rc
)
target_link_libraries(Obex PRIVATE obexcore gdi32 comctl32 comdlg32 dwmapi uxtheme wtsapi32)
# The manifest is embedded through Obex.rc.
target_link_options(Obex PRIVATE /MANIFEST:NO)

add_executable(ObexTests tests/tests.cpp)
target_link_libraries(ObexTests PRIVATE obexcore)
