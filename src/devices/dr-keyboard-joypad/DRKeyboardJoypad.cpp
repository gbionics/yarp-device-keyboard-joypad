// SPDX-FileCopyrightText: Generative Bionics S.R.L.
// SPDX-License-Identifier: LicenseRef-GenerativeBionics-AllRightsReserved

#include "DRKeyboardJoypad.h"

#include <yarp/os/LogStream.h>
#include <yarp/os/Property.h>

namespace {
YARP_LOG_COMPONENT(DRKEYBOARDJOYPAD, "dinrail.device.DRKeyboardJoypad")
}

DRKeyboardJoypad::~DRKeyboardJoypad() { close(); }

bool DRKeyboardJoypad::open(yarp::os::Searchable &config) {
  yarp::os::Property props;
  props.fromString(config.toString());
  props.put("device", "keyboardJoypad");

  if (!m_driver.open(props)) {
    yCError(DRKEYBOARDJOYPAD, "Failed to open inner keyboardJoypad device");
    return false;
  }

  if (!m_driver.view(m_iJoypad) || !m_iJoypad) {
    yCError(DRKEYBOARDJOYPAD,
            "Failed to acquire IJoypadController from keyboardJoypad");
    m_driver.close();
    return false;
  }

  return true;
}

bool DRKeyboardJoypad::close() {
  m_iJoypad = nullptr;
  if (m_driver.isValid()) {
    m_driver.close();
  }
  return true;
}

bool DRKeyboardJoypad::getAxisCount(unsigned int &count) {
  return m_iJoypad != nullptr && m_iJoypad->getAxisCount(count);
}

bool DRKeyboardJoypad::getButtonCount(unsigned int &count) {
  return m_iJoypad != nullptr && m_iJoypad->getButtonCount(count);
}

bool DRKeyboardJoypad::getHatCount(unsigned int &count) {
  return m_iJoypad != nullptr && m_iJoypad->getHatCount(count);
}

bool DRKeyboardJoypad::getAxis(unsigned int axis_id, double &value) {
  return m_iJoypad != nullptr && m_iJoypad->getAxis(axis_id, value);
}

bool DRKeyboardJoypad::getButton(unsigned int button_id, float &value) {
  return m_iJoypad != nullptr && m_iJoypad->getButton(button_id, value);
}

bool DRKeyboardJoypad::getHat(unsigned int hat_id, unsigned char &value) {
  return m_iJoypad != nullptr && m_iJoypad->getHat(hat_id, value);
}

bool DRKeyboardJoypad::prepareForReconnect() { return true; }

bool DRKeyboardJoypad::consumeDisconnectEvent() { return false; }
