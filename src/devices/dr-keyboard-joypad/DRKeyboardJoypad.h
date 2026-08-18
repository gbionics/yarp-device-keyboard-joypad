// SPDX-FileCopyrightText: Generative Bionics S.R.L.
// SPDX-License-Identifier: LicenseRef-GenerativeBionics-AllRightsReserved

#ifndef DINRAIL_DR_KEYBOARD_JOYPAD_H
#define DINRAIL_DR_KEYBOARD_JOYPAD_H

#include <dinrail/IJoypadControl.h>

#include <yarp/dev/DeviceDriver.h>
#include <yarp/dev/IJoypadController.h>
#include <yarp/dev/PolyDriver.h>

/**
 * YARP device that wraps the `keyboardJoypad` plugin and exposes it through
 * the `dinrail::IJoypadControl` interface.
 *
 * All configuration parameters are forwarded verbatim to the inner
 * `keyboardJoypad` device, so the `[JOYPAD_DEVICE]` block in the .ini file
 * keeps exactly the same keys. Only the `device` line changes to
 * `dr_keyboard_joypad`.
 *
 * `reinitJoystickSubsystem()` is a no-op and `isDeviceRemoved()` always
 * returns false. This keeps JoypadModule compatible with both SDL and
 * keyboard-backed devices through a single interface.
 */
class DRKeyboardJoypad : public yarp::dev::DeviceDriver,
                         public dinrail::IJoypadControl {
public:
  DRKeyboardJoypad() = default;
  ~DRKeyboardJoypad() override;

  bool open(yarp::os::Searchable &config) override;
  bool close() override;

  bool getAxisCount(unsigned int &count) override;
  bool getButtonCount(unsigned int &count) override;
  bool getHatCount(unsigned int &count) override;
  bool getAxis(unsigned int axis_id, double &value) override;
  bool getButton(unsigned int button_id, float &value) override;
  bool getHat(unsigned int hat_id, unsigned char &value) override;

  bool prepareForReconnect() override;
  bool consumeDisconnectEvent() override;

private:
  yarp::dev::PolyDriver m_driver;
  yarp::dev::IJoypadController *m_iJoypad{nullptr};
};

#endif // DINRAIL_DR_KEYBOARD_JOYPAD_H
