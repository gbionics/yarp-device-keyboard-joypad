# SPDX-FileCopyrightText: Generative Bionics S.R.L.
# SPDX-License-Identifier: LicenseRef-GenerativeBionics-AllRightsReserved

yarp_prepare_plugin(dr_keyboard_joypad
  CATEGORY device
  TYPE DRKeyboardJoypad
  INCLUDE DRKeyboardJoypad.h
  DEFAULT ON
  INTERNAL
  QUIET
)

yarp_add_plugin(yarp_dr-keyboard-joypad)

target_sources(yarp_dr-keyboard-joypad
  PRIVATE
    DRKeyboardJoypad.cpp
    DRKeyboardJoypad.h
)

target_link_libraries(yarp_dr-keyboard-joypad
  PRIVATE
    dinrail::dinrail
    YARP::YARP_os
    YARP::YARP_dev
)

target_include_directories(yarp_dr-keyboard-joypad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(yarp_dr-keyboard-joypad PUBLIC cxx_std_17)

yarp_install(
  TARGETS yarp_dr-keyboard-joypad
  EXPORT yarp-device-keyboard-joypad
  COMPONENT yarp-device-keyboard-joypad
  LIBRARY DESTINATION ${YARP_DYNAMIC_PLUGINS_INSTALL_DIR}
  ARCHIVE DESTINATION ${YARP_STATIC_PLUGINS_INSTALL_DIR}
  YARP_INI DESTINATION ${YARP_PLUGIN_MANIFESTS_INSTALL_DIR}
)
