#
# Copyright (C) 2019-2020 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

add_subdirectory(tracing)
add_subdirectory(metrics)
add_subdirectory(sysman)
add_subdirectory(pin)

set(L0_TOOLS_SOURCES
    ${L0_SRCS_TOOLS_TRACING}
    ${L0_SRCS_TOOLS_METRICS}
    ${L0_SRCS_TOOLS_PIN}
    ${L0_SRCS_TOOLS_SYSMAN}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools_init.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tools_init_imp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools_init_imp.h
)

target_sources(${TARGET_NAME_L0}
    PRIVATE
        ${L0_TOOLS_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
)

# Make our source files visible to parent
set_property(GLOBAL PROPERTY L0_TOOLS_SOURCES ${L0_TOOLS_SOURCES})
