# Copyright (C) 2024 Istituto Italiano di Tecnologia (IIT)
# All rights reserved.
#
# This software may be modified and distributed under the terms of the
# BSD-2-Clause license. See the accompanying LICENSE file for details.

yarp_prepare_plugin(keyboardJoypad
  CATEGORY device
  TYPE yarp::dev::KeyboardJoypad
  INCLUDE KeyboardJoypad.h
  DEFAULT ON
  INTERNAL
  QUIET
  EXTRA_CONFIG
    WRAPPER=JoypadControlServer
)

set(yarp_keyboard-joypad_SRCS
  KeyboardJoypad.cpp
  KeyboardJoypadLogComponent.cpp
)

set(yarp_keyboard-joypad_HDRS
  KeyboardJoypad.h
  KeyboardJoypadLogComponent.h
)


yarp_add_plugin(yarp_keyboard-joypad)


target_sources(yarp_keyboard-joypad
  PRIVATE
    ${yarp_keyboard-joypad_SRCS}
    ${yarp_keyboard-joypad_HDRS}
)

target_link_libraries(yarp_keyboard-joypad
  PRIVATE
    dinrail::dinrail
    YARP::YARP_os
    YARP::YARP_sig
    YARP::YARP_dev
    YARP::YARP_math
    glfw
    GLEW::GLEW
    OpenGL::GL
    imgui::imgui
)

if (NOT WIN32)
    target_link_libraries(yarp_keyboard-joypad PRIVATE ${X11_LIBRARIES})
endif()

target_include_directories(yarp_keyboard-joypad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (USE_VENDORED_IMGUI)
   target_include_directories(yarp_keyboard-joypad PUBLIC ${imgui_SOURCE_DIR}/backends) # The files in the backends folder are available when installed
endif()

target_compile_features(yarp_keyboard-joypad PUBLIC cxx_std_20) #C++20 is used for the designated initialization of structs

yarp_install(
  TARGETS yarp_keyboard-joypad
  EXPORT yarp-device-keyboard-joypad
  COMPONENT yarp-device-keyboard-joypad
  LIBRARY DESTINATION ${YARP_DYNAMIC_PLUGINS_INSTALL_DIR}
  ARCHIVE DESTINATION ${YARP_STATIC_PLUGINS_INSTALL_DIR}
  YARP_INI DESTINATION ${YARP_PLUGIN_MANIFESTS_INSTALL_DIR}
)
