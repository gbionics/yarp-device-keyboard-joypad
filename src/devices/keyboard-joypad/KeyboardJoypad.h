/*
 * Copyright (C) 2024 Istituto Italiano di Tecnologia (IIT)
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the
 * BSD-2-Clause license. See the accompanying LICENSE file for details.
 */

#ifndef YARP_DEV_KEYBOARDJOYPAD_H
#define YARP_DEV_KEYBOARDJOYPAD_H

#include <memory>

#include <dinrail/IJoypadControl.h>

#include <yarp/dev/DeviceDriver.h>
#include <yarp/dev/IJoypadController.h>
#include <yarp/os/PeriodicThread.h>
#include <yarp/dev/ServiceInterfaces.h>

namespace yarp {
    namespace dev {
        class KeyboardJoypad;
    }
}

class yarp::dev::KeyboardJoypad : public yarp::dev::DeviceDriver,
    public yarp::os::PeriodicThread,
    public yarp::dev::IService,
    public yarp::dev::IJoypadController,
    public dinrail::IJoypadControl
{
public:
    KeyboardJoypad();

    virtual ~KeyboardJoypad();

    // yarp::dev::DeviceDriver methods
    virtual bool open(yarp::os::Searchable& cfg) override;
    virtual bool close() override;

    // yarp::os::RateThread methods
    virtual bool threadInit() override;
    virtual void threadRelease() override;
    virtual void run() override;

    //  yarp::dev::IService methods
    virtual bool startService() override;
    virtual bool updateService() override;
    virtual bool stopService() override;

    // yarp::dev::IJoypadController methods
    virtual bool getAxisCount(unsigned int& axis_count) override;
    virtual bool getButtonCount(unsigned int& button_count) override;
    virtual bool getTrackballCount(unsigned int& trackball_count) override;
    virtual bool getHatCount(unsigned int& hat_count) override;
    virtual bool getTouchSurfaceCount(unsigned int& touch_count) override;
    virtual bool getStickCount(unsigned int& stick_count) override;
    virtual bool getStickDoF(unsigned int stick_id, unsigned int& dof) override;
    virtual bool getButton(unsigned int button_id, float& value) override;
    virtual bool getTrackball(unsigned int trackball_id, yarp::sig::Vector& value) override;
    virtual bool getHat(unsigned int hat_id, unsigned char& value) override;
    virtual bool getAxis(unsigned int axis_id, double& value) override;
    virtual bool getStick(unsigned int stick_id, yarp::sig::Vector& value, JoypadCtrl_coordinateMode coordinate_mode) override;
    virtual bool getTouch(unsigned int touch_id, yarp::sig::Vector& value) override;

    // dinrail::IJoypadControl reconnect support (no-op for the keyboard backend)
    virtual bool reconnect() override;
    virtual bool getLastEvent(dinrail::JoypadDeviceEvent& event) override;

private:

    class Impl;
    std::unique_ptr<Impl> m_pimpl;
};


#endif // YARP_DEV_KEYBOARDJOYPAD_H
