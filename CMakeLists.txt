cmake_minimum_required(VERSION 3.16)

project(NexusLink VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NEXUSLINK_ENABLE_GUI "Enable GUI" ON)
option(NEXUSLINK_ENABLE_SANITIZERS "Enable ASan+UBSan for Debug builds (Apple only)" OFF)

if(NOT NEXUSLINK_ENABLE_GUI)
    message(FATAL_ERROR "NexusLink requires GUI. Set -DNEXUSLINK_ENABLE_GUI=ON")
endif()

set(NEXUSLINK_SOURCES
    src/main.cpp
    src/core/app.cpp
    src/network/socket.cpp
    src/network/discovery.cpp
    src/network/connection.cpp
    src/chat/chat.cpp
    src/file/file_sender.cpp
    src/file/file_receiver.cpp
    src/security/encryption.cpp
    src/utils/logger.cpp
    src/ui/gui.cpp
)

add_executable(app ${NEXUSLINK_SOURCES})

target_include_directories(app PRIVATE
    src
)

if(APPLE AND NEXUSLINK_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(app PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(app PRIVATE -fsanitize=address,undefined)
endif()

if(APPLE)
    set_target_properties(app PROPERTIES
        OUTPUT_NAME NEXUSLINK
    )
else()
    set_target_properties(app PROPERTIES
        OUTPUT_NAME NexusLink
    )
endif()

find_package(Threads REQUIRED)
target_link_libraries(app PRIVATE Threads::Threads)

if(WIN32)
    target_compile_definitions(app PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(app PRIVATE ws2_32)
    set(NEXUSLINK_DIST_EXE_NAME "NexusLink.exe")
elseif(APPLE)
    set(NEXUSLINK_DIST_EXE_NAME "NEXUSLINK")
else()
    set(NEXUSLINK_DIST_EXE_NAME "NexusLink")
endif()

target_compile_definitions(app PRIVATE NEXUSLINK_ENABLE_GUI)

set(NEXUSLINK_IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(NEXUSLINK_IMGUI_BACKENDS_DIR ${NEXUSLINK_IMGUI_DIR}/backends)

target_sources(app PRIVATE
    ${NEXUSLINK_IMGUI_DIR}/imgui.cpp
    ${NEXUSLINK_IMGUI_DIR}/imgui_draw.cpp
    ${NEXUSLINK_IMGUI_DIR}/imgui_tables.cpp
    ${NEXUSLINK_IMGUI_DIR}/imgui_widgets.cpp
    ${NEXUSLINK_IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
    ${NEXUSLINK_IMGUI_BACKENDS_DIR}/imgui_impl_opengl3.cpp
)

target_include_directories(app PRIVATE
    ${NEXUSLINK_IMGUI_DIR}
    ${NEXUSLINK_IMGUI_BACKENDS_DIR}
)

if(APPLE)
    find_package(glfw3 REQUIRED)
    find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h REQUIRED)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(app PRIVATE GL_SILENCE_DEPRECATION)
    target_include_directories(app PRIVATE ${GLFW_INCLUDE_DIR})
    target_link_libraries(app PRIVATE glfw OpenGL::GL)
elseif(WIN32)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
    )
    FetchContent_MakeAvailable(glfw)
    find_package(OpenGL REQUIRED)
    target_link_libraries(app PRIVATE glfw OpenGL::GL)
else()
    find_package(glfw3 REQUIRED)
    find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h REQUIRED)
    find_package(OpenGL REQUIRED)
    target_include_directories(app PRIVATE ${GLFW_INCLUDE_DIR})
    target_link_libraries(app PRIVATE glfw OpenGL::GL)
endif()

# Keep output path predictable for packaging steps.
if(APPLE)
    set_target_properties(app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
    set_target_properties(app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

add_custom_target(package_dist
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist/data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist/data/downloads
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist/data/uploads
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist/data/logs
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:app> ${CMAKE_SOURCE_DIR}/dist/${NEXUSLINK_DIST_EXE_NAME}
    DEPENDS app
    COMMENT "Assembling distribution folder in dist/"
)

add_custom_target(smoke_test
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/data/logs
    COMMAND ${CMAKE_COMMAND} -E env NEXUSLINK_SMOKE_TEST=1 $<TARGET_FILE:app>
    DEPENDS app
    COMMENT "Running NexusLink smoke test"
)

set(NEXUSLINK_LEGACY_CLEANUP_SCRIPT ${CMAKE_BINARY_DIR}/cleanup_legacy_builds.cmake)
file(WRITE ${NEXUSLINK_LEGACY_CLEANUP_SCRIPT}
"set(ROOT \"${CMAKE_SOURCE_DIR}\")\n"
"set(ARCHIVE \"${CMAKE_SOURCE_DIR}/dist/legacy-builds\")\n"
"file(MAKE_DIRECTORY \"${CMAKE_SOURCE_DIR}/dist\")\n"
"file(MAKE_DIRECTORY \"${CMAKE_SOURCE_DIR}/dist/legacy-builds\")\n"
"foreach(name build build-win app app_gui)\n"
"  set(src \"${ROOT}/${name}\")\n"
"  if(EXISTS ${src})\n"
"    file(RENAME ${src} \"${ARCHIVE}/${name}\")\n"
"  endif()\n"
"endforeach()\n"
)

add_custom_target(cleanup_legacy_builds
    COMMAND ${CMAKE_COMMAND} -P ${NEXUSLINK_LEGACY_CLEANUP_SCRIPT}
    COMMENT "Moving legacy root build artifacts into dist/legacy-builds"
)

if(APPLE)
    set(NEXUSLINK_APP_BUNDLE_DIR ${CMAKE_SOURCE_DIR}/dist/NEXUSLINK.app)
    set(NEXUSLINK_APP_PLIST ${CMAKE_BINARY_DIR}/Info.plist)
    set(NEXUSLINK_BUNDLE_FIXUP_SCRIPT ${CMAKE_BINARY_DIR}/fixup_nexuslink_bundle.cmake)

    set(NEXUSLINK_BUNDLE_SEARCH_DIRS "/opt/homebrew/lib;/opt/homebrew/opt/glfw/lib;/usr/local/lib")

    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} --print-file-name=libclang_rt.asan_osx_dynamic.dylib
        OUTPUT_VARIABLE NEXUSLINK_ASAN_DYLIB
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "${NEXUSLINK_ASAN_DYLIB}")
        get_filename_component(NEXUSLINK_ASAN_DIR "${NEXUSLINK_ASAN_DYLIB}" DIRECTORY)
        list(APPEND NEXUSLINK_BUNDLE_SEARCH_DIRS "${NEXUSLINK_ASAN_DIR}")
    endif()

    list(JOIN NEXUSLINK_BUNDLE_SEARCH_DIRS ";" NEXUSLINK_BUNDLE_SEARCH_DIRS_JOINED)

    file(WRITE ${NEXUSLINK_APP_PLIST}
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
"<plist version=\"1.0\">\n"
"<dict>\n"
"    <key>CFBundleExecutable</key>\n"
"    <string>NEXUSLINK</string>\n"
"    <key>CFBundleIdentifier</key>\n"
"    <string>com.nexuslink.app</string>\n"
"    <key>CFBundleName</key>\n"
"    <string>NexusLink</string>\n"
"    <key>CFBundlePackageType</key>\n"
"    <string>APPL</string>\n"
"    <key>CFBundleShortVersionString</key>\n"
"    <string>${PROJECT_VERSION}</string>\n"
"</dict>\n"
"</plist>\n"
    )

    file(WRITE ${NEXUSLINK_BUNDLE_FIXUP_SCRIPT}
"include(BundleUtilities)\n"
"set(BU_CHMOD_BUNDLE_ITEMS ON)\n"
"fixup_bundle(\"${NEXUSLINK_APP_BUNDLE_DIR}\" \"\" \"${NEXUSLINK_BUNDLE_SEARCH_DIRS_JOINED}\")\n"
"verify_app(\"${NEXUSLINK_APP_BUNDLE_DIR}\")\n"
    )

    add_custom_target(package_macos_app
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NEXUSLINK_APP_BUNDLE_DIR}/Contents/MacOS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NEXUSLINK_APP_BUNDLE_DIR}/Contents/Resources
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NEXUSLINK_APP_BUNDLE_DIR}/Contents/Frameworks
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:app> ${NEXUSLINK_APP_BUNDLE_DIR}/Contents/MacOS/NEXUSLINK
        COMMAND ${CMAKE_COMMAND} -E copy ${NEXUSLINK_APP_PLIST} ${NEXUSLINK_APP_BUNDLE_DIR}/Contents/Info.plist
        COMMAND ${CMAKE_COMMAND} -P ${NEXUSLINK_BUNDLE_FIXUP_SCRIPT}
        COMMAND /usr/bin/codesign --force --deep --sign - ${NEXUSLINK_APP_BUNDLE_DIR}
        DEPENDS app
        COMMENT "Packaging macOS app bundle at dist/NEXUSLINK.app"
    )
endif()
