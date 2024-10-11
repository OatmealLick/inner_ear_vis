# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(rhiwindow LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

qt_standard_project_setup()

# assimp time
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)  # Disable building the Assimp tools
set(ASSIMP_BUILD_TESTS OFF)          # Disable building tests
add_subdirectory("${CMAKE_SOURCE_DIR}/vendor/assimp")
include_directories("${CMAKE_SOURCE_DIR}/vendor/assimp/include")

message(STATUS "Disabling Metal feature in Qt")
add_compile_definitions(QT_NO_METAL)

qt_add_executable(rhiwindow
    main.cpp
    rhiwindow.cpp rhiwindow.h
)

set_target_properties(rhiwindow PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(rhiwindow PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
        assimp
)

set_source_files_properties("shaders/prebuilt/color.vert.qsb"
    PROPERTIES QT_RESOURCE_ALIAS "color.vert.qsb"
)
set_source_files_properties("shaders/prebuilt/color.frag.qsb"
    PROPERTIES QT_RESOURCE_ALIAS "color.frag.qsb"
)
set_source_files_properties("shaders/prebuilt/quad.vert.qsb"
    PROPERTIES QT_RESOURCE_ALIAS "quad.vert.qsb"
)
set_source_files_properties("shaders/prebuilt/quad.frag.qsb"
    PROPERTIES QT_RESOURCE_ALIAS "quad.frag.qsb"
)
set_source_files_properties("resources/bunny.obj"
    PROPERTIES QT_RESOURCE_ALIAS "bunny.obj"
)
qt_add_resources(rhiwindow "rhiwindow"
    PREFIX
        "/"
    FILES
        "shaders/prebuilt/color.vert.qsb"
        "shaders/prebuilt/color.frag.qsb"
        "shaders/prebuilt/quad.vert.qsb"
        "shaders/prebuilt/quad.frag.qsb"
        "resources/bunny.obj"
)

install(TARGETS rhiwindow
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET rhiwindow
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
