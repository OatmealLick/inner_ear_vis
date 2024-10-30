# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(inner_ear_vis LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(Qt6 COMPONENTS ShaderTools)

qt_standard_project_setup()

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)  # Disable building the Assimp tools
set(ASSIMP_BUILD_TESTS OFF)          # Disable building tests
add_subdirectory("${CMAKE_SOURCE_DIR}/vendor/assimp")
include_directories("${CMAKE_SOURCE_DIR}/vendor/assimp/include")

message(STATUS "Disabling Metal feature in Qt")
add_compile_definitions(QT_NO_METAL)

qt_add_executable(inner_ear_vis
        main.cpp
        inner_ear_vis.cpp
        inner_ear_vis.h
        Entity.cpp
        Entity.h
        util.h
        Camera.cpp
        Camera.h
        vendor/stb_image.h
        vendor/easing/easing.cpp
        vendor/easing/easing.h
)

set_target_properties(inner_ear_vis PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_link_libraries(inner_ear_vis PRIVATE
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
set_source_files_properties("resources/inner_ear.fbx"
        PROPERTIES QT_RESOURCE_ALIAS "inner_ear.fbx"
)

qt_add_resources(inner_ear_vis "inner_ear_vis"
        PREFIX
        "/"
        FILES
        "resources/inner_ear.fbx"
)
qt_add_shaders(inner_ear_vis "inner_ear_vis_shaders"
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

install(TARGETS inner_ear_vis
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
        TARGET inner_ear_vis
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
