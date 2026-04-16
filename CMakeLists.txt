cmake_minimum_required(VERSION 3.28)
project(logos-node-basecamp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ── Qt ────────────────────────────────────────────────────────────────────────
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.9.3/gcc_64")
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick QuickControls2 Network RemoteObjects)
qt_standard_project_setup()

# ── Nix store paths ───────────────────────────────────────────────────────────
if(DEFINED ENV{LOGOS_LIBLOGOS_HEADERS})
    set(LOGOS_LIBLOGOS_HEADERS "$ENV{LOGOS_LIBLOGOS_HEADERS}")
else()
    set(LOGOS_LIBLOGOS_HEADERS
        "/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include")
endif()

if(DEFINED ENV{LOGOS_CPP_SDK_ROOT})
    set(LOGOS_CPP_SDK "$ENV{LOGOS_CPP_SDK_ROOT}")
else()
    set(LOGOS_CPP_SDK
        "/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk")
endif()

# ── Plugin ────────────────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
    src/plugin/NodePlugin.h
    src/plugin/NodePlugin.cpp
)

add_library(node_plugin MODULE ${PLUGIN_SOURCES})

set_target_properties(node_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "node_plugin"
)

target_include_directories(node_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

target_link_libraries(node_plugin PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Network
    "${LOGOS_CPP_SDK}/lib/liblogos_sdk.a"
    dl pthread
)

configure_file(
    src/plugin/plugin_metadata.json
    ${CMAKE_CURRENT_BINARY_DIR}/src/plugin/plugin_metadata.json
    COPYONLY
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_node_plugin
    tests/test_node_plugin.cpp
    tests/logos_api_stub.cpp
    src/plugin/NodePlugin.h
    src/plugin/NodePlugin.cpp
    "${LOGOS_CPP_SDK}/include/cpp/logos_api.h"
)

target_include_directories(test_node_plugin PRIVATE
    src
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

target_link_libraries(test_node_plugin PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Network
    dl pthread
)

add_test(NAME test_node_plugin COMMAND test_node_plugin)

# ── Install paths ─────────────────────────────────────────────────────────────
set(LOGOS_APP_DATA "$ENV{HOME}/.local/share/Logos/LogosApp")
set(NODE_MODULE_DIR "${LOGOS_APP_DATA}/modules/logos_node")
set(NODE_UI_DIR     "${LOGOS_APP_DATA}/plugins/node_ui")

# Clean old module dir on reinstall
install(CODE "
    set(_modules_dir \"\\$ENV{DESTDIR}${LOGOS_APP_DATA}/modules\")
    file(GLOB _old \"\${_modules_dir}/logos_node*\")
    foreach(_dir \${_old})
        file(REMOVE_RECURSE \"\${_dir}\")
    endforeach()
")

install(TARGETS node_plugin LIBRARY DESTINATION "${NODE_MODULE_DIR}")
install(FILES
    modules/logos_node/manifest.json
    modules/logos_node/metadata.json
    modules/logos_node/plugin_metadata.json
    modules/logos_node/variant
    DESTINATION "${NODE_MODULE_DIR}"
)
install(FILES
    plugins/node_ui/manifest.json
    plugins/node_ui/metadata.json
    plugins/node_ui/Main.qml
    plugins/node_ui/variant
    DESTINATION "${NODE_UI_DIR}"
)
install(FILES
    assets/icons/node.png
    DESTINATION "${NODE_UI_DIR}/icons"
)

# Patch RUNPATH so Qt libs resolve correctly when loaded from LogosApp AppImage
find_program(PATCHELF_EXECUTABLE patchelf)
if(PATCHELF_EXECUTABLE)
    install(CODE "
        set(_plugin \"${NODE_MODULE_DIR}/node_plugin.so\")
        set(_build_so \"${CMAKE_BINARY_DIR}/node_plugin.so\")
        if(EXISTS \"\${_plugin}\")
            execute_process(
                COMMAND bash -c \"ldd '\${_build_so}' | grep nix/store | awk '{print \\\$3}' | xargs -I{} dirname {} | sort -u | tr '\\n' ':' | sed 's/:$//'\"
                OUTPUT_VARIABLE _nix_paths
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(_qt_lib \"\$ENV{HOME}/Qt/6.9.3/gcc_64/lib\")
            set(_rpath \"\\\$ORIGIN:\${_nix_paths}:\${_qt_lib}:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu\")
            execute_process(
                COMMAND ${PATCHELF_EXECUTABLE} --set-rpath \"\${_rpath}\" \"\${_plugin}\"
            )
            message(STATUS \"Patched RUNPATH on node_plugin.so\")
        endif()
    ")
endif()

# Mirror to LogosBasecamp
install(CODE "
    set(_mod_src \"${NODE_MODULE_DIR}\")
    set(_mod_dst \"\$ENV{HOME}/.local/share/Logos/LogosBasecamp/modules/logos_node\")
    file(COPY \"\${_mod_src}/\" DESTINATION \"\${_mod_dst}\")

    set(_ui_src \"${NODE_UI_DIR}\")
    set(_ui_dst \"\$ENV{HOME}/.local/share/Logos/LogosBasecamp/plugins/node_ui\")
    file(GLOB _ui_files \"\${_ui_src}/*\")
    foreach(_f \${_ui_files})
        file(COPY \"\${_f}\" DESTINATION \"\${_ui_dst}\")
    endforeach()
    message(STATUS \"Mirror to LogosBasecamp complete\")
")
