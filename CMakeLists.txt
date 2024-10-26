# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(rhiwindow LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(Qt6 COMPONENTS ShaderTools)

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
        stb_image.h
        Entity.cpp
        Entity.h
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

set_source_files_properties("shaders/color.vert.qsb"
        PROPERTIES QT_RESOURCE_ALIAS "color.vert.qsb"
)
set_source_files_properties("shaders/color.frag.qsb"
        PROPERTIES QT_RESOURCE_ALIAS "color.frag.qsb"
)
set_source_files_properties("shaders/quad.vert.qsb"
        PROPERTIES QT_RESOURCE_ALIAS "quad.vert.qsb"
)
set_source_files_properties("shaders/quad.frag.qsb"
        PROPERTIES QT_RESOURCE_ALIAS "quad.frag.qsb"
)
set_source_files_properties("shaders/ray.vert.qsb"
        PROPERTIES QT_RESOURCE_ALIAS "ray.vert.qsb"
)
set_source_files_properties("shaders/ray.frag.qsb"
        PROPERTIES QT_RESOURCE_ALIAS "ray.frag.qsb"
)
set_source_files_properties("resources/bunny.obj"
        PROPERTIES QT_RESOURCE_ALIAS "bunny.obj"
)
set_source_files_properties("resources/inner_ear.fbx"
        PROPERTIES QT_RESOURCE_ALIAS "inner_ear.fbx"
)

qt_add_resources(rhiwindow "rhiwindow"
    PREFIX
        "/"
    FILES
        "resources/bunny.obj"
        "resources/inner_ear.fbx"
)
qt_add_shaders(rhiwindow "rhiwindow_shaders"
        PREFIX
        "/"
        FILES
        "shaders/color.vert"
        "shaders/color.frag"
        "shaders/quad.vert"
        "shaders/quad.frag"
        "shaders/ray.vert"
        "shaders/ray.frag"
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
