/*
 * Copyright (C) 2021 Istituto Italiano di Tecnologia (IIT)
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the
 * BSD-2-Clause license. See the accompanying LICENSE file for details.
 */

#define GL_GLEXT_PROTOTYPES
#define GL3_PROTOTYPES
#define GL_SILENCE_DEPRECATION

#include <GL/glew.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stdio.h>

#include <mutex>
#include <atomic>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <sstream>
#include <iomanip>

#include <yarp/os/LogStream.h>

#include <KeyboardJoypad.h>
#include <KeyboardJoypadLogComponent.h>

struct ButtonValue
{
    int sign = 1;
    size_t index = 0;
};

enum class ButtonType
{
    REGULAR,
    TOGGLE,
};


struct ButtonState {
    std::string alias;
    ButtonType type{ ButtonType::REGULAR };
    std::vector<ImGuiKey> keys;
    std::vector<ButtonValue> values;
    std::vector<ButtonValue> joypadAxisInputs;
    std::vector<int> joypadButtonIndices;
    int col{ 0 };
    bool active{ false };
    bool buttonPressed{ false };

    float deadzone(float input, float deadzone) const
    {
        if (input > deadzone)
            return (input - deadzone) / (1.0f - deadzone);
        else return 0.0;
    }

    void render(const ImVec4& button_active_color, const ImVec4& button_inactive_color,
                const ImVec2& buttonSize, bool hold_active, double joypadDeadzone,
                const std::vector<float>& joypadAxisValues, const std::vector<bool>& joypadButtonValues,
                std::vector<double>& outputValues)
    {
        bool regularButton = type == ButtonType::REGULAR;
        bool toggleButton = type == ButtonType::TOGGLE;
        bool anyKeyPressed = false;
        bool anyKeyReleased = false;
        for (ImGuiKey key : keys)
        {
            if (ImGui::IsKeyPressed(key))
            {
                anyKeyPressed = true;
            }
            if (ImGui::IsKeyReleased(key))
            {
                anyKeyReleased = true;
            }
        }

        for (int i : joypadButtonIndices)
        {
            if (i >= 0 && i < joypadButtonValues.size())
            {
                if (joypadButtonValues[static_cast<size_t>(i)])
                {
                    anyKeyPressed = true;
                }
                else
                {
                    anyKeyReleased = true;
                }
            }
            else if (i >= 0 && joypadButtonValues.size() > 0)
            {
                yCErrorOnce(KEYBOARDJOYPAD) << "The joypad button index" << i << "is out of range.";
            }
        }

        float valueFromJoypadAxes = 0.0;
        for (auto& axis : joypadAxisInputs)
        {
            if (axis.index >= 0 && axis.index < joypadAxisValues.size())
            {
                valueFromJoypadAxes += deadzone(axis.sign * joypadAxisValues[static_cast<size_t>(axis.index)], static_cast<float>(joypadDeadzone));
            }
            else if (axis.index >= 0 && joypadAxisValues.size() > 0)
            {
                yCError(KEYBOARDJOYPAD) << "The joypad axis index" << axis.index << "is out of range.";
                axis.index = -1;
            }
        }

        if (anyKeyPressed)
        {
            buttonPressed = true;
            if (toggleButton || (regularButton && !hold_active))
            {
                active = true;
            }
            else
            {
                active = !active;
            }
        }
        else if (buttonPressed && anyKeyReleased)
        {
            buttonPressed = false;
            if (toggleButton || (regularButton && !hold_active))
                active = false;
        }

        ImGuiStyle& style = ImGui::GetStyle();
        const ImVec4& buttonColor = active || valueFromJoypadAxes > 0 ? button_active_color : button_inactive_color;
        style.Colors[ImGuiCol_Button] = buttonColor;
        style.Colors[ImGuiCol_ButtonHovered] = buttonColor;
        style.Colors[ImGuiCol_ButtonActive] = buttonColor;

        // Create a button
        bool buttonReleased = ImGui::Button(alias.c_str(), buttonSize);
        bool buttonKeptPressed = ImGui::IsItemActive();

        if (buttonReleased && (toggleButton || (regularButton && hold_active)))
        {
            active = !active; //Toggle the button
        }
        else if (regularButton && buttonKeptPressed && !hold_active) //The button is clicked and is not a toggling button
        {
            active = true;
        }
        else if (regularButton && !buttonPressed && !hold_active) //The button is not clicked and is not a toggling button
        {
            active = false;
        }

        for (auto& value : values)
        {
            outputValues[value.index] += value.sign * (active + valueFromJoypadAxes);
        }
    }
};

struct ButtonsTable
{
    std::vector<std::vector<ButtonState>> rows;
    int numberOfColumns { 0 };
    std::string name;
};

static bool parseFloat(yarp::os::Searchable& cfg, const std::string& key, float min_value, float max_value, float& value)
{
    if (!cfg.check(key))
    {
        yCInfo(KEYBOARDJOYPAD) << "The key" << key << "is not present in the configuration file."
                               << "Using the default value:" << value;
        return true;
    }

    if (!cfg.find(key).isFloat64() && !cfg.find(key).isInt64() && !cfg.find(key).isInt32())
    {
        yCError(KEYBOARDJOYPAD) << "The value of " << key << " is not a float";
        return false;
    }
    float input = static_cast<float>(cfg.find(key).asFloat64());
    if (input < min_value || input > max_value)
    {
        yCError(KEYBOARDJOYPAD) << "The value of " << key << " is out of range. It should be between" << min_value << "and" << max_value;
        return false;
    }
    value = input;
    return true;
}

static bool parseInt(yarp::os::Searchable& cfg, const std::string& key, int min_value, int max_value, int& value)
{
    if (!cfg.check(key))
    {
        yCInfo(KEYBOARDJOYPAD) << "The key" << key << "is not present in the configuration file."
                               << "Using the default value:" << value;
        return true;
    }

    if (!cfg.find(key).isInt64() && !cfg.find(key).isInt32())
    {
        yCError(KEYBOARDJOYPAD) << "The value of " << key << " is not an integer";
        return false;
    }
    int input = static_cast<int>(cfg.find(key).asInt64());
    if (input < min_value || input > max_value)
    {
        yCError(KEYBOARDJOYPAD) << "The value of " << key << " is out of range. It should be between" << min_value << "and" << max_value;
        return false;
    }
    value = input;
    return true;
}

struct Settings {
    float button_size = 100;
    float min_button_size = 50;
    float max_button_size = 200;
    float font_multiplier = 1.0;
    float min_font_multiplier = 0.5;
    float max_font_multiplier = 4.0;
    float gui_period = 0.033f;
    float deadzone = 0.1f;
    float padding = 100;
    int window_width = 1280;
    int window_height = 720;
    int buttons_per_row = 3;
    bool allow_window_closing = false;
    std::atomic<bool> single_threaded { false };
    std::vector<int> joypad_indices;

    bool parseFromConfigFile(yarp::os::Searchable& cfg)
    {
        if (!parseFloat(cfg, "button_size", 1.f, 1e5f, button_size))
        {
            return false;
        }

        if (!parseFloat(cfg, "min_button_size", 1.f, 1e5f, min_button_size))
        {
            return false;
        }

        if (!parseFloat(cfg, "max_button_size", 1.f, 1e5f, max_button_size))
        {
            return false;
        }

        if (!parseFloat(cfg, "font_multiplier", 0.01f, 1e5f, font_multiplier))
        {
            return false;
        }

        if (!parseFloat(cfg, "min_font_multiplier", 0.01f, 1e5f, min_font_multiplier))
        {
            return false;
        }

        if (!parseFloat(cfg, "max_font_multiplier", 0.01f, 1e5f, max_font_multiplier))
        {
            return false;
        }

        if (!parseFloat(cfg, "gui_period", 1e-3f, 1e5f, gui_period))
        {
            return false;
        }

        if (!parseFloat(cfg, "joypad_deadzone", 0.f, 1.f, deadzone))
        {
            return false;
        }

        if (!parseFloat(cfg, "padding", 0.f, 1e5f, padding))
        {
            return false;
        }

        if (!parseInt(cfg, "window_width", 1, static_cast<int>(1e4), window_width))
        {
            return false;
        }

        if (!parseInt(cfg, "window_height", 1, static_cast<int>(1e4), window_height))
        {
            return false;
        }

        if (!parseInt(cfg, "buttons_per_row", 1, 100, buttons_per_row))
        {
            return false;
        }

        if (cfg.check("allow_window_closing"))
        {
            allow_window_closing = cfg.find("allow_window_closing").isNull() || cfg.find("allow_window_closing").asBool();
        }
        else
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"allow_window_closing\" is not present in the configuration file."
                                   << "Using the default value:" << allow_window_closing;
        }

        //If macOs, the GUI thread must be the main thread. Hence use no GUI thread
#ifdef __APPLE__
        single_threaded = true;
        yCWarning(KEYBOARDJOYPAD) << "In macOS the GUI thread should be the main thread. Hence, we are using true as default for \"no_gui_thread\"";

#endif //__APPLE__


        if (cfg.check("no_gui_thread"))
        {
            single_threaded = cfg.find("no_gui_thread").isNull() || cfg.find("no_gui_thread").asBool();
        }
        else
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"no_gui_thread\" is not present in the configuration file."
                                   << "Using the default value:" << static_cast<bool>(single_threaded);
        }

        if (single_threaded && allow_window_closing)
        {
            yCError(KEYBOARDJOYPAD) << "The configuration file is invalid. The keys \"no_gui_thread\" and \"allow_window_closing\" cannot be both true.";
            return false;
        }

        if (cfg.check("joypad_indices"))
        {
            yarp::os::Value joypadsValue = cfg.find("joypad_indices");
            if (joypadsValue.isInt32() || joypadsValue.isInt64())
            {
                int joypad_index = static_cast<int>(joypadsValue.asInt64());
                if (joypad_index > GLFW_JOYSTICK_LAST)
                {
                    yCError(KEYBOARDJOYPAD) << "The value of \"joypad_indices\" is out of range."
                        << "It should be between" << GLFW_JOYSTICK_1 << "and" << GLFW_JOYSTICK_LAST;
                    return false;
                }
                if (joypad_index >= GLFW_JOYSTICK_1)
                {
                    joypad_indices.push_back(joypad_index);
                }
            }
            else if (!joypadsValue.isList())
            {
                yCError(KEYBOARDJOYPAD) << "\"joypad_indices\" is found but it is neither an int nor a list.";
                return false;
            }

            yarp::os::Bottle* joypads_index_list = joypadsValue.asList();

            for (size_t i = 0; i < joypads_index_list->size(); i++)
            {
                if (!joypads_index_list->get(i).isInt64() && !joypads_index_list->get(i).isInt32())
                {
                    yCError(KEYBOARDJOYPAD) << "The value at index" << i << "of the \"joypad_indices\" list is not an integer.";
                    return false;
                }

                int joypad_index = static_cast<int>(joypads_index_list->get(i).asInt64());
                if (joypad_index < GLFW_JOYSTICK_1 || joypad_index > GLFW_JOYSTICK_LAST)
                {
                    yCError(KEYBOARDJOYPAD) << "The value at index" << i << "of the joypads_index list is out of range."
                        << "It should be between" << GLFW_JOYSTICK_1 << "and" << GLFW_JOYSTICK_LAST;
                    return false;
                }

                joypad_indices.push_back(joypad_index);
            }
        }
        else
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"joypads_index\" is not present in the configuration file."
                                   << "Using only the joypad with index 0 (if present).";
            joypad_indices.push_back(GLFW_JOYSTICK_1);
        }

        return true;
    }
};

enum class Axis
{
    WS = 0,
    AD = 1,
    UP_DOWN = 2,
    LEFT_RIGHT = 3
};

struct AxisSettings
{
    int sign;
    size_t index;
};

struct AxesSettings
{
    std::unordered_map<Axis, std::vector<AxisSettings>> axes;
    size_t number_of_axes = 0;

    std::string wasd_label = "WASD";
    std::string arrows_label = "Arrows";
    int ad_joypad_axis_index = 0;
    int ws_joypad_axis_index = 1;
    int left_right_joypad_axis_index = 2;
    int up_down_joypad_axis_index = 3;

    bool parseFromConfigFile(yarp::os::Searchable& cfg)
    {
        if (!cfg.check("axes"))
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"axes\" is not present in the configuration file. Enabling both wasd and the arrows.";
            axes[Axis::AD].push_back({+1, 0});
            axes[Axis::WS].push_back({+1, 1});
            axes[Axis::LEFT_RIGHT].push_back({+1, 2});
            axes[Axis::UP_DOWN].push_back({+1, 3});
            number_of_axes = 4;
        }
        else
        {
            if (!cfg.find("axes").isList())
            {
                yCError(KEYBOARDJOYPAD) << "The value of \"axes\" is not a list";
                return false;
            }

            yarp::os::Bottle* axes_list = cfg.find("axes").asList();

            for (size_t i = 0; i < axes_list->size(); i++)
            {
                if (!axes_list->get(i).isString())
                {
                    yCError(KEYBOARDJOYPAD) << "The value at index" << i << "of the axes list is not a string.";
                    return false;
                }

                std::string axis = axes_list->get(i).asString();

                //Check if the first character is a - or a + and remove it
                int sign = +1;
                if (axis[0] == '-' || axis[0] == '+')
                {
                    sign = axis[0] == '-' ? -1 : +1;
                    axis = axis.substr(1);
                }

                std::transform(axis.begin(), axis.end(), axis.begin(), ::tolower);

                if (axis == "ws")
                {
                    axes[Axis::WS].push_back({ sign, i });
                }
                else if (axis == "ad")
                {
                    axes[Axis::AD].push_back({ sign, i });
                }
                else if (axis == "up_down")
                {
                    axes[Axis::UP_DOWN].push_back({ sign, i });
                }
                else if (axis == "left_right")
                {
                    axes[Axis::LEFT_RIGHT].push_back({ sign, i });
                }
                else if (axis != "" && axis != "none")
                {
                    yCError(KEYBOARDJOYPAD) << "The value of the axes list (" << axis << ") is not a valid axis."
                        << "Allowed values(\"ws\", \"ad\", \"up_down\", \"left_right\","
                        << "eventually with a + or - as prefix, \"none\" and \"\")";
                    return false;
                }
            }
            number_of_axes = axes_list->size();
        }

        if (cfg.check("wasd_label"))
        {
            wasd_label = cfg.find("wasd_label").asString();
        }
        else
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"wasd_label\" is not present in the configuration file."
                                   << "Using the default value:" << wasd_label;
        }

        if (cfg.check("arrows_label"))
        {
            arrows_label = cfg.find("arrows_label").asString();
        }
        else
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"arrows_label\" is not present in the configuration file."
                                   << "Using the default value:" << arrows_label;
        }

        std::vector<std::pair<std::string, int&>> joypad_axis_index = { {"ad_joypad_axis_index", ad_joypad_axis_index},
                                                                        {"ws_joypad_axis_index", ws_joypad_axis_index},
                                                                        {"left_right_joypad_axis_index", left_right_joypad_axis_index},
                                                                        {"up_down_joypad_axis_index", up_down_joypad_axis_index} };

        for (auto& [name, index] : joypad_axis_index)
        {
            if (!parseInt(cfg, name, -1, 100, index))
            {
                return false;
            }
        }

        return true;
    }
};

struct JoypadInfo
{
    std::string name;
    int index;
    int axes;
    int buttons;
    size_t axes_offset;
    size_t buttons_offset;
    bool active;
};

class yarp::dev::KeyboardJoypad::Impl
{
public:
    GLFWwindow* window = nullptr;

    std::atomic_bool need_to_close{false}, closed{false}, initialized{false};

    std::mutex mutex;

    ImVec4 button_inactive_color;
    ImVec4 button_active_color;

    Settings settings;
    AxesSettings axes_settings;

    std::vector<ButtonsTable> sticks;
    std::vector<std::vector<size_t>> sticks_to_axes;
    ButtonsTable buttons;
    ButtonState ctrl_button;
    std::vector<double> ctrl_value;
    std::vector<double> axes_values;
    std::vector<std::vector<double>> sticks_values;
    std::vector<double> buttons_values;

    std::vector<JoypadInfo> joypads;
    std::vector<float> joypad_axis_values;
    std::vector<bool> joypad_button_values;
    bool using_joypad = false;

    double last_gui_update_time = 0.0;
    std::thread::id gui_thread_id;

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);


    static void GLMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*) {
        yCError(KEYBOARDJOYPAD, "GL CALLBACK: %s source = 0x%x, type = 0x%x, id = 0x%x, severity = 0x%x, message = %s",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            source, type, id, severity, message);
    }

    static void glfwErrorCallback(int error, const char* description) {
        yCError(KEYBOARDJOYPAD, "GLFW error %d: %s", error, description);
    }

    bool parseButtonsSettings(yarp::os::Searchable& cfg)
    {
        buttons.name = "Buttons";
        if (!cfg.check("buttons"))
        {
            yCInfo(KEYBOARDJOYPAD) << "The key \"buttons\" is not present in the configuration file. No buttons will be created.";
            return true;
        }

        if (!cfg.find("buttons").isList())
        {
            yCError(KEYBOARDJOYPAD) << "The value of \"buttons\" is not a list";
            return false;
        }

        yarp::os::Bottle* buttons_list = cfg.find("buttons").asList();

        std::unordered_map<std::string, std::pair<size_t, size_t>> buttons_map; //map existing buttons to their location

        int col = 0;
        for (size_t i = 0; i < buttons_list->size(); i++)
        {
            std::string buttons_with_alias;
            if (!buttons_list->get(i).isString())
            {
                if (buttons_list->get(i).isInt64() || buttons_list->get(i).isInt32())
                {
                    buttons_with_alias = std::to_string(buttons_list->get(i).asInt64());
                }
                else
                {
                    yCError(KEYBOARDJOYPAD) << "The value at index" << i << "of the buttons list is not a string.";
                    return false;
                }
            }
            else
            {
                buttons_with_alias = buttons_list->get(i).asString();
            }

            if (buttons_with_alias == "" || buttons_with_alias == "none")
            {
                continue;
            }

            std::string buttons_keys = buttons_with_alias;
            std::string alias = "";

            size_t pos = buttons_with_alias.find(':');
            bool have_alias = false;
            if (pos != std::string::npos)
            {
                buttons_keys = buttons_with_alias.substr(0, pos);
                alias = buttons_with_alias.substr(pos + 1);
                have_alias = true;
            }
            else
            {
                alias = buttons_keys;
            }

            std::transform(buttons_keys.begin(), buttons_keys.end(), buttons_keys.begin(), ::toupper);

            std::vector<std::string> buttons_key_list;
            std::string delimiter = "-";
            pos = buttons_keys.find(delimiter);
            while (pos != std::string::npos)
            {
                buttons_key_list.push_back(buttons_keys.substr(0, pos));
                buttons_keys.erase(0, pos + delimiter.length());
                pos = buttons_keys.find(delimiter);
            }
            buttons_key_list.push_back(buttons_keys);

            ButtonState newButton;
            newButton.values.push_back({ .sign = 1, .index = i });

            std::unordered_map<std::string, ImGuiKey> supportedButtons = {
                {"SPACE", ImGuiKey_Space},
                {"ENTER", ImGuiKey_Enter},
                {"ESCAPE", ImGuiKey_Escape},
                {"BACKSPACE", ImGuiKey_Backspace},
                {"DELETE", ImGuiKey_Delete},
                {"LEFT", ImGuiKey_LeftArrow},
                {"RIGHT", ImGuiKey_RightArrow},
                {"UP", ImGuiKey_UpArrow},
                {"DOWN", ImGuiKey_DownArrow},
                {"TAB", ImGuiKey_Tab}
            };

            std::string parsedButtons;
            for (auto& button : buttons_key_list)
            {
                bool parsed = true;
                if (button.size() == 1 && button[0] >= 'A' && button[0] <= 'Z')
                {
                    newButton.keys.push_back(static_cast<ImGuiKey>(ImGuiKey_A + button[0] - 'A'));
                }
                else if (button.size() && button[0] >= '0' && button[0] <= '9')
                {
                    newButton.keys.push_back(static_cast<ImGuiKey>(ImGuiKey_0 + button[0] - '0'));
                    newButton.keys.push_back(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + button[0] - '0'));
                }
                else if (button.size() > 1 && button[0] == 'J' && std::find_if(button.begin() + 1,
                    button.end(), [](unsigned char c) { return !std::isdigit(c); }) == button.end()) //J followed by a number
                {
                    int joypad_button = std::stoi(button.substr(1));
                    newButton.joypadButtonIndices.push_back(joypad_button);
                }
                else if (supportedButtons.find(button) != supportedButtons.end())
                {
                    newButton.keys.push_back(supportedButtons[button]);
                }
                else
                {
                    parsed = false;
                }

                if (parsed)
                {
                    if (!parsedButtons.empty())
                    {
                        parsedButtons += ", " + button;
                    }
                    else
                    {
                        parsedButtons = button;
                    }
                }
            }

            if (!parsedButtons.empty() && have_alias)
            {
                alias += " (" + parsedButtons + ")";
            }
            else if (!parsedButtons.empty() && !have_alias)
            {
                alias = parsedButtons;
            }

            newButton.alias = alias;

            if (buttons_map.find(newButton.alias) != buttons_map.end())
            {
                size_t button_row = buttons_map[newButton.alias].first;
                size_t button_col = buttons_map[newButton.alias].second;
                buttons.rows[button_row][button_col].values.push_back(newButton.values.front());
            }
            else
            {
                if (buttons.rows.empty() || buttons.rows.back().size() == settings.buttons_per_row)
                {
                    buttons.rows.emplace_back();
                    col = 0;
                }
                newButton.col = col++;
                buttons.rows.back().push_back(newButton);
                buttons_map[newButton.alias] = std::make_pair(buttons.rows.size() - 1, buttons.rows.back().size() - 1);
            }
        }
        buttons_values.resize(buttons_list->size(), 0.0);
        if (!buttons.rows.empty())
        {
            ctrl_button = {.alias = "Hold (Ctrl)", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl}, .values = {{.sign = 1, .index = 0}} };
            ctrl_value.resize(1, 0.0);
        }

        return true;
    }

    void prepareWindow(const ImVec2& position, const std::string& name)
    {
        ImGui::SetNextWindowPos(position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name.c_str(), 0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(settings.font_multiplier);
    }

    void renderButtonsTable(ButtonsTable& buttons_table, bool hold_active, double joypadDeadzone,
                            const std::vector<float>& joypadAxisValues, const std::vector<bool>& joypadButtonValues,
                            std::vector<double>& values) const
    {
        //Define the size of the buttons
        ImVec2 buttonSize(settings.button_size, settings.button_size);
        const int& n_cols = buttons_table.numberOfColumns;

        ImGui::BeginTable(buttons_table.name.c_str(), n_cols, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingMask_);

        for (auto& row : buttons_table.rows)
        {
            ImGui::TableNextRow();

            for (auto& button : row)
            {
                ImGui::TableSetColumnIndex(button.col);

                button.render(button_active_color, button_inactive_color, buttonSize, hold_active, joypadDeadzone, joypadAxisValues, joypadButtonValues, values);
            }
            if (row.empty())
            {
                ImGui::TableSetColumnIndex(0);
                ImGui::Dummy(buttonSize);
            }
        }
        ImGui::EndTable();
    }

    bool initialize()
    {
        glfwSetErrorCallback(&KeyboardJoypad::Impl::glfwErrorCallback);
        if (!glfwInit()) {
            yCError(KEYBOARDJOYPAD, "Unable to initialize GLFW");
            return false;
        }

        this->window = glfwCreateWindow(this->settings.window_width, this->settings.window_height,
            "YARP Keyboard as Joypad Device Window", nullptr, nullptr);
        if (!this->window) {
            yCError(KEYBOARDJOYPAD, "Could not create window");
            return false;
        }

        glfwMakeContextCurrent(this->window);
        glfwSwapInterval(1);

        // Initialize the GLEW OpenGL 3.x bindings
        // GLEW must be initialized after creating the window
        glewExperimental = GL_TRUE;
        GLenum err = glewInit();
        if (err != GLEW_OK) {
            yCError(KEYBOARDJOYPAD) << "glewInit failed, aborting.";
            return false;
        }
        yCInfo(KEYBOARDJOYPAD) << "Using GLEW" << (const char*)glewGetString(GLEW_VERSION);

        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE); //This is to ignore message 0x20071 about the use of the VIDEO memory

        glDebugMessageCallback(&KeyboardJoypad::Impl::GLMessageCallback, NULL);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glEnable(GL_DEBUG_OUTPUT);


        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(this->window, true);
        ImGui_ImplOpenGL3_Init();

        for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; ++i) {
            if (glfwJoystickPresent(i)) {
                int axes_count, button_count;
                glfwGetJoystickAxes(i, &axes_count);
                glfwGetJoystickButtons(i, &button_count);
                std::string name {glfwGetJoystickName(i)};
                this->joypads.push_back({ .name = name, .index = i, .axes = axes_count, .buttons = button_count });
                yCInfo(KEYBOARDJOYPAD) << "Joypad" << name << "is available (index" << i
                                       << "axes =" << axes_count << "buttons = " << button_count << ").";
            }
        }

        if (!this->joypads.size()) {
            yCInfo(KEYBOARDJOYPAD) << "No joypad found.";
        }

        size_t axes_offset = 0;
        size_t buttons_offset = 0;

        for (int& joypad_index : this->settings.joypad_indices)
        {
            if (joypad_index >= this->joypads.size())
            {
                yCWarning(KEYBOARDJOYPAD) << "The joypad with index" << joypad_index << "is not available. It will be skipped";
                continue;
            }
            this->joypads[static_cast<size_t>(joypad_index)].axes_offset = axes_offset;
            this->joypads[static_cast<size_t>(joypad_index)].buttons_offset = buttons_offset;
            this->joypads[static_cast<size_t>(joypad_index)].active = true;
            axes_offset += this->joypads[static_cast<size_t>(joypad_index)].axes;
            buttons_offset += this->joypads[static_cast<size_t>(joypad_index)].buttons;
            this->using_joypad = true;
        }
        this->joypad_axis_values.resize(axes_offset, 0.0);
        this->joypad_button_values.resize(buttons_offset, false);

        this->button_inactive_color = ImGui::GetStyle().Colors[ImGuiCol_Button];
        this->button_active_color = ImVec4(0.7f, 0.5f, 0.3f, 1.0f);

        this->gui_thread_id = std::this_thread::get_id();
        this->initialized = true;

        return true;
    }

    void prepareFrame()
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        for (double& value : this->axes_values)
        {
            value = 0;
        }

        for (double& value : this->buttons_values)
        {
            value = 0;
        }

        for (auto& value : this->ctrl_value)
        {
            value = 0;
        }

        for (auto& value : this->joypad_axis_values)
        {
            value = 0;
        }

        for (size_t i = 0; i < this->joypad_button_values.size(); ++i) //The for(auto& v : ..) does not work with bool arrays
        {
            this->joypad_button_values[i] = false;
        }
    }

    void update()
    {

        if (!this->initialized || this->gui_thread_id != std::this_thread::get_id())
        {
            return;
        }

        this->prepareFrame();

        if (this->using_joypad)
        {
            for (auto& joypad : this->joypads)
            {
                if (!glfwJoystickPresent(joypad.index) || !joypad.active)
                {
                    continue;
                }

                int new_axes, new_buttons;
                const float* axes = glfwGetJoystickAxes(joypad.index, &new_axes);
                const unsigned char* buttons = glfwGetJoystickButtons(joypad.index, &new_buttons);

                for (size_t i = 0; i < std::min(joypad.axes, new_axes); ++i)
                {
                    this->joypad_axis_values[joypad.axes_offset + i] = axes[i];
                }

                for (size_t i = 0; i < std::min(joypad.buttons, new_buttons); ++i)
                {
                    this->joypad_button_values[joypad.buttons_offset + i] = buttons[i] == GLFW_PRESS;
                }
            }
        }

        ImVec2 position(this->settings.padding, this->settings.padding);
        float button_table_height = position.y;
        for (auto& stick : this->sticks)
        {
            position.y = this->settings.padding; //Keep the sticks on the save level
            this->prepareWindow(position, stick.name);
            this->renderButtonsTable(stick, false, this->settings.deadzone,
                       this->joypad_axis_values, this->joypad_button_values,this->axes_values);
            ImGui::End();
            position.x += stick.numberOfColumns * this->settings.button_size + this->settings.padding; // Move the next table to the right (n columns + 1 space)
            position.y += stick.rows.size() * this->settings.button_size + this->settings.padding; // Move the next table down (n rows + 1 space)
            button_table_height = std::max(button_table_height, position.y);
        }

        //Clamp axes values to the range -1, 1
        for (auto& axis_value : this->axes_values)
        {
            if (axis_value > 1)
            {
                axis_value = 1;
            }
            else if (axis_value < -1)
            {
                axis_value = -1;
            }
        }

        //Update sticks values from axes values
        for (size_t i = 0; i < this->sticks_to_axes.size(); ++i)
        {
            for (size_t j = 0; j < this->sticks_to_axes[i].size(); j++)
            {
                this->sticks_values[i][j] = this->axes_values[this->sticks_to_axes[i][j]];
            }
        }

        if (!this->buttons.rows.empty())
        {
            position.y = this->settings.padding; //Keep the buttons on the save level of the sticks
            this->prepareWindow(position, this->buttons.name);
            ImGui::BeginTable("Buttons_layout", 1, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingMask_ | ImGuiTableFlags_BordersInner);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            this->ctrl_button.render(this->button_active_color, this->button_inactive_color, ImVec2(this->settings.button_size, this->settings.button_size), false, this->settings.deadzone,
                       this->joypad_axis_values, this->joypad_button_values, this->ctrl_value);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool hold_active = this->ctrl_value.front() > 0;
            this->renderButtonsTable(this->buttons, hold_active, this->settings.deadzone,
                       this->joypad_axis_values, this->joypad_button_values, this->buttons_values);
            ImGui::EndTable();
            ImGui::End();
        }

        //Apply clamping to buttons values in the range 0, 1 and round them to 0 or 1
        for (auto& button_value : this->buttons_values)
        {
            if (button_value > 0)
            {
                button_value = 1;
            }
            else
            {
                button_value = 0;
            }
        }

        position.x = this->settings.padding; //Reset the x position
        position.y = button_table_height; //Move the next table down

        this->prepareWindow(position, "Settings");
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Application average %.1f ms/frame (%.1f FPS)", io.DeltaTime * 1000.0f, io.Framerate);

        int width, height;
        glfwGetWindowSize(this->window, &width, &height);

        ImGui::Text("Window size: %d x %d", width, height);
        ImGui::SliderFloat("Button size", &this->settings.button_size, this->settings.min_button_size, this->settings.max_button_size);
        ImGui::SliderFloat("Font multiplier", &this->settings.font_multiplier, this->settings.min_font_multiplier, this->settings.max_font_multiplier);
        if (this->using_joypad)
        {
            ImGui::SliderFloat("Joypad deadzone", &this->settings.deadzone, 0.0, 1.0);
            // Display the joypad values
            std::string connectedJoypads = "Connected joypads: ";
            for (size_t i = 0; i < this->joypads.size(); ++i)
            {
                connectedJoypads += this->joypads[i].name;
                if (i != this->joypads.size() - 1)
                {
                    connectedJoypads += ", ";
                }
            }
            ImGui::Separator();
            ImGui::Text(connectedJoypads.c_str());
            std::string axes_values = "Joypad axes values: ";
            for (size_t i = 0; i < this->joypad_axis_values.size(); ++i)
            {
                // Print the values of the axes in the format "axis_index: value" with a 1 decimal precision
                std::stringstream stream;
                stream << std::fixed << std::setprecision(2) << this->joypad_axis_values[i];
                std::string sign = this->joypad_axis_values[i] >= 0 ? "+" : "";
                axes_values += "<" + std::to_string(i) + "> " + sign + stream.str();
                if (i != this->joypad_axis_values.size() - 1)
                {
                    axes_values += ", ";
                }
            }
            ImGui::Text(axes_values.c_str());
            std::string buttons_values = "Joypad buttons values: ";
            for (size_t i = 0; i < this->joypad_button_values.size(); ++i)
            {
                buttons_values += "<" + std::to_string(i) + "> " + (this->joypad_button_values[i] ? "1" : "0");
                if (i != this->joypad_button_values.size() - 1)
                {
                    buttons_values += ", ";
                }
            }
            ImGui::Text(buttons_values.c_str());
        }
        ImGui::Separator();
        std::string output_axes_values = "Output axes values: ";
        for (size_t i = 0; i < this->axes_values.size(); ++i)
        {
            // Print the values of the axes in the format "axis_index: value" with a 1 decimal precision
            std::stringstream stream;
            stream << std::fixed << std::setprecision(2) << this->axes_values[i];
            std::string sign = this->axes_values[i] >= 0 ? "+" : "";
            output_axes_values += "<" + std::to_string(i) + "> " + sign + stream.str();
            if (i != this->axes_values.size() - 1)
            {
                output_axes_values += ", ";
            }
        }
        ImGui::Text(output_axes_values.c_str());
        std::string output_buttons_values = "Output buttons values: ";
        if (this->buttons_values.empty())
        {
            output_buttons_values += "None";
        }
        for (size_t i = 0; i < this->buttons_values.size(); ++i)
        {
            std::stringstream stream;
            stream << std::fixed << std::setprecision(1) << this->buttons_values[i];
            output_buttons_values += "<" + std::to_string(i) + "> " + stream.str();
            if (i != this->buttons_values.size() - 1)
            {
                output_buttons_values += ", ";
            }
        }
        ImGui::Text(output_buttons_values.c_str());
        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(this->window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(this->clear_color.x * this->clear_color.w, this->clear_color.y * this->clear_color.w, this->clear_color.z * this->clear_color.w, this->clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(this->window);

        this->last_gui_update_time = yarp::os::Time::now();
    }

    bool needUpdate() const
    {
        if (this->closed)
        {
            return false;
        }
        return yarp::os::Time::now() - this->last_gui_update_time > this->settings.gui_period;
    }

    void close()
    {
        if (this->closed || !this->initialized)
            return;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (this->window)
        {
            glfwDestroyWindow(this->window);
            glfwTerminate();
            this->window = nullptr;
        }

        this->closed = true;
    }

};



yarp::dev::KeyboardJoypad::KeyboardJoypad()
    : yarp::dev::DeviceDriver(),
    yarp::os::PeriodicThread(0.033, yarp::os::ShouldUseSystemClock::Yes)
{
    m_pimpl = std::make_unique<Impl>();
}

yarp::dev::KeyboardJoypad::~KeyboardJoypad()
{
    this->stop();
    m_pimpl->close();
}

bool yarp::dev::KeyboardJoypad::open(yarp::os::Searchable& cfg)
{
    if (!m_pimpl->settings.parseFromConfigFile(cfg))
    {
        return false;
    }

    if (!m_pimpl->axes_settings.parseFromConfigFile(cfg))
    {
        return false;
    }

    m_pimpl->buttons.numberOfColumns = m_pimpl->settings.buttons_per_row;

    if (!m_pimpl->parseButtonsSettings(cfg))
    {
        return false;
    }

    int ws = m_pimpl->axes_settings.axes.find(Axis::WS) != m_pimpl->axes_settings.axes.end();
    int ad = m_pimpl->axes_settings.axes.find(Axis::AD) != m_pimpl->axes_settings.axes.end();
    int up_down = m_pimpl->axes_settings.axes.find(Axis::UP_DOWN) != m_pimpl->axes_settings.axes.end();
    int left_right = m_pimpl->axes_settings.axes.find(Axis::LEFT_RIGHT) != m_pimpl->axes_settings.axes.end();

    m_pimpl->axes_values.resize(static_cast<size_t>(m_pimpl->axes_settings.number_of_axes), 0.0);
    m_pimpl->sticks_to_axes.clear();

    if (ws || ad)
    {
        m_pimpl->sticks_to_axes.emplace_back();
        m_pimpl->sticks_values.emplace_back();
        ButtonsTable& wasd = m_pimpl->sticks.emplace_back();
        wasd.name = m_pimpl->axes_settings.wasd_label;
        wasd.numberOfColumns = ad ? 3 : 1; //Number of columns
        if (ws)
        {
            std::vector<ButtonValue> values;
            for (AxisSettings& ws_settings : m_pimpl->axes_settings.axes[Axis::WS])
            {
                values.push_back({ .sign = -ws_settings.sign, .index = ws_settings.index });
            }
            wasd.rows.push_back({ {.alias = "W", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_W}, .values = values,
                                   .joypadAxisInputs = {{.sign = -1, .index = static_cast<size_t>(m_pimpl->axes_settings.ws_joypad_axis_index)}},
                                   .col = ad} });
        }
        if (ad)
        {
            std::vector<ButtonValue> a_values;
            std::vector<ButtonValue> d_values;
            for (AxisSettings& ws_settings : m_pimpl->axes_settings.axes[Axis::AD])
            {
                a_values.push_back({ .sign = -ws_settings.sign, .index = ws_settings.index });
                d_values.push_back({ .sign = ws_settings.sign, .index = ws_settings.index });
            }
            if (a_values.size() > 0)
            {
                m_pimpl->sticks_to_axes.back().push_back(a_values.front().index);
                m_pimpl->sticks_values.back().push_back(0);
            }

            wasd.rows.push_back({ {.alias = "A", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_A}, .values = a_values,
                                   .joypadAxisInputs = {{.sign = -1, .index = static_cast<size_t>(m_pimpl->axes_settings.ad_joypad_axis_index)}},
                                   .col = 0},
                                  {.alias = "D", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_D}, .values = d_values,
                                   .joypadAxisInputs = {{.sign = +1, .index = static_cast<size_t>(m_pimpl->axes_settings.ad_joypad_axis_index)}},
                                   .col = 2} });
        }
        else
        {
            wasd.rows.emplace_back(); //empty row
        }
        if (ws)
        {
            std::vector<ButtonValue> values;
            for (AxisSettings& ws_settings : m_pimpl->axes_settings.axes[Axis::WS])
            {
                values.push_back({ .sign = ws_settings.sign, .index = ws_settings.index });
            }
            if (values.size() > 0)
            {
                m_pimpl->sticks_to_axes.back().push_back(values.front().index);
                m_pimpl->sticks_values.back().push_back(0);
            }

            wasd.rows.push_back({ {.alias = "S", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_S}, .values = values,
                                   .joypadAxisInputs = {{.sign = +1, .index = static_cast<size_t>(m_pimpl->axes_settings.ws_joypad_axis_index)}},
                                   .col = ad}});
        }
    }

    if (up_down || left_right)
    {
        m_pimpl->sticks_to_axes.emplace_back();
        m_pimpl->sticks_values.emplace_back();
        ButtonsTable& arrows = m_pimpl->sticks.emplace_back();
        arrows.name = m_pimpl->axes_settings.arrows_label;
        arrows.numberOfColumns = left_right ? 3 : 1; //Number of columns
        if (up_down)
        {
            std::vector<ButtonValue> values;
            for (AxisSettings& ws_settings : m_pimpl->axes_settings.axes[Axis::UP_DOWN])
            {
                values.push_back({ .sign = -ws_settings.sign, .index = ws_settings.index });
            }
            arrows.rows.push_back({ {.alias = "top", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_UpArrow}, .values = values,
                                     .joypadAxisInputs = {{.sign = -1, .index = static_cast<size_t>(m_pimpl->axes_settings.up_down_joypad_axis_index)}},
                                     .col = left_right} });
        }
        if (left_right)
        {
            std::vector<ButtonValue> l_values;
            std::vector<ButtonValue> r_values;
            for (AxisSettings& ws_settings : m_pimpl->axes_settings.axes[Axis::LEFT_RIGHT])
            {
                l_values.push_back({ .sign = -ws_settings.sign, .index = ws_settings.index });
                r_values.push_back({ .sign = ws_settings.sign, .index = ws_settings.index });
            }
            if (l_values.size() > 0)
            {
                m_pimpl->sticks_to_axes.back().push_back(l_values.front().index);
                m_pimpl->sticks_values.back().push_back(0);
            }
            arrows.rows.push_back({ {.alias = "left", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_LeftArrow}, .values = l_values,
                                     .joypadAxisInputs = {{.sign = -1, .index = static_cast<size_t>(m_pimpl->axes_settings.left_right_joypad_axis_index)}},
                                     .col = 0},
                                    {.alias = "right", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_RightArrow}, .values = r_values,
                                     .joypadAxisInputs = {{.sign = +1, .index = static_cast<size_t>(m_pimpl->axes_settings.left_right_joypad_axis_index)}},
                                     .col = 2} });
        }
        else
        {
            arrows.rows.emplace_back(); //empty row
        }
        if (up_down)
        {
            std::vector<ButtonValue> values;
            for (AxisSettings& ws_settings : m_pimpl->axes_settings.axes[Axis::UP_DOWN])
            {
                values.push_back({ .sign = ws_settings.sign, .index = ws_settings.index });
            }
            if (values.size() > 0)
            {
                m_pimpl->sticks_to_axes.back().push_back(values.front().index);
                m_pimpl->sticks_values.back().push_back(0);
            }
            arrows.rows.push_back({ {.alias = "bottom", .type = ButtonType::TOGGLE, .keys = {ImGuiKey_DownArrow}, .values = values,
                                     .joypadAxisInputs = {{.sign = +1, .index = static_cast<size_t>(m_pimpl->axes_settings.up_down_joypad_axis_index)}},
                                     .col = left_right} });
        }
    }

    if (m_pimpl->settings.single_threaded)
    {
        yCInfo(KEYBOARDJOYPAD) << "The device is running in single threaded mode.";
    }
    else
    {
        yCInfo(KEYBOARDJOYPAD) << "The device is running in multi threaded mode.";
        this->setPeriod(m_pimpl->settings.gui_period);

        // Start the thread
        if (!this->start()) {
            yCError(KEYBOARDJOYPAD) << "Thread start failed, aborting.";
            this->close();
            return false;
        }
    }

    return true;
}

bool yarp::dev::KeyboardJoypad::close()
{
    yCInfo(KEYBOARDJOYPAD) << "Closing the device";
    this->askToStop();
    if (m_pimpl->settings.single_threaded)
    {
        m_pimpl->close();
    }
    return true;
}

bool yarp::dev::KeyboardJoypad::threadInit()
{
    if (m_pimpl->closed || m_pimpl->settings.single_threaded)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_pimpl->mutex);

    return m_pimpl->initialize();
}

void yarp::dev::KeyboardJoypad::threadRelease()
{
    if (m_pimpl->closed || m_pimpl->settings.single_threaded)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_pimpl->mutex);

    m_pimpl->close();
}

void yarp::dev::KeyboardJoypad::run()
{
    if (m_pimpl->closed || m_pimpl->settings.single_threaded)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_pimpl->mutex);
        if (m_pimpl->settings.allow_window_closing)
        {
            m_pimpl->need_to_close = glfwWindowShouldClose(m_pimpl->window);
        }
    }
    double period = 0;
    double desired_period = 0;

    if (!m_pimpl->need_to_close)
    {
        std::lock_guard<std::mutex> lock(m_pimpl->mutex);

        m_pimpl->update();

        desired_period = getPeriod();
        period = getEstimatedUsed();

    }
    else
    {
        this->close();
    }

    if (period > desired_period)
    {
        yCWarningThrottle(KEYBOARDJOYPAD, 5.0, "The period of the GUI is higher than the period of the thread. The GUI will be updated at a lower rate.");
        yarp::os::Time::delay(1e-3); //Sleep for 1 ms to avoid the other threads to go to starvation
    }


}

bool yarp::dev::KeyboardJoypad::startService()
{
    //To let the device driver knowing that it need to poll updateService continuosly
    return false;
}

bool yarp::dev::KeyboardJoypad::updateService()
{
    //To let the device driver that we are still alive
    if (m_pimpl->settings.single_threaded && !m_pimpl->closed)
    {
        std::lock_guard<std::mutex> lock(m_pimpl->mutex);
        if (!m_pimpl->initialized)
        {
            if (!m_pimpl->initialize())
            {
                return false;
            }
        }
        m_pimpl->update();
    }
    return !m_pimpl->closed;
}

bool yarp::dev::KeyboardJoypad::stopService()
{
    yCInfo(KEYBOARDJOYPAD) << "Stopping the service";
    return this->close();
}

bool yarp::dev::KeyboardJoypad::getAxisCount(unsigned int& axis_count)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    axis_count = static_cast<unsigned int>(m_pimpl->axes_values.size());
    return true;
}

bool yarp::dev::KeyboardJoypad::getButtonCount(unsigned int& button_count)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    button_count = static_cast<unsigned int>(m_pimpl->buttons_values.size());
    return true;
}

bool yarp::dev::KeyboardJoypad::getTrackballCount(unsigned int& trackball_count)
{
    trackball_count = 0;
    return true;
}

bool yarp::dev::KeyboardJoypad::getHatCount(unsigned int& hat_count)
{
    hat_count = 0;
    return true;
}

bool yarp::dev::KeyboardJoypad::getTouchSurfaceCount(unsigned int& touch_count)
{
    touch_count = 0;
    return true;
}

bool yarp::dev::KeyboardJoypad::getStickCount(unsigned int& stick_count)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    stick_count = static_cast<unsigned int>(m_pimpl->sticks_to_axes.size());
    return true;
}

bool yarp::dev::KeyboardJoypad::getStickDoF(unsigned int stick_id, unsigned int& dof)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    if (stick_id >= m_pimpl->sticks_to_axes.size())
    {
        yCError(KEYBOARDJOYPAD) << "The stick with id" << stick_id << "does not exist.";
        return false;
    }

    dof = static_cast<unsigned int>(m_pimpl->sticks_to_axes[stick_id].size());

    return true;
}

bool yarp::dev::KeyboardJoypad::getButton(unsigned int button_id, float& value)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    if (!m_pimpl->initialized)
    {
        if (!m_pimpl->initialize())
        {
            return false;
        }
    }
    if (m_pimpl->settings.single_threaded && m_pimpl->needUpdate())
    {
        m_pimpl->update();
    }
    if (button_id >= m_pimpl->buttons_values.size())
    {
        yCError(KEYBOARDJOYPAD) << "The button with id" << button_id << "does not exist.";
        return false;
    }
    value = static_cast<float>(m_pimpl->buttons_values[button_id]);
    return true;
}

bool yarp::dev::KeyboardJoypad::getTrackball(unsigned int /*trackball_id*/, yarp::sig::Vector& /*value*/)
{
    yCError(KEYBOARDJOYPAD) << "This device does not consider trackballs.";
    return false;
}

bool yarp::dev::KeyboardJoypad::getHat(unsigned int /*hat_id*/, unsigned char& /*value*/)
{
    yCError(KEYBOARDJOYPAD) << "This device does not consider hats.";
    return false;
}

bool yarp::dev::KeyboardJoypad::getAxis(unsigned int axis_id, double& value)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    if (!m_pimpl->initialized)
    {
        if (!m_pimpl->initialize())
        {
            return false;
        }
    }
    if (m_pimpl->settings.single_threaded && m_pimpl->needUpdate())
    {
        m_pimpl->update();
    }
    if (axis_id >= m_pimpl->axes_values.size())
    {
        yCError(KEYBOARDJOYPAD) << "The axis with id" << axis_id << "does not exist.";
        return false;
    }
    value = m_pimpl->axes_values[axis_id];
    return true;
}

bool yarp::dev::KeyboardJoypad::getStick(unsigned int stick_id, yarp::sig::Vector& value, JoypadCtrl_coordinateMode coordinate_mode)
{
    std::lock_guard<std::mutex> lock(m_pimpl->mutex);
    if (!m_pimpl->initialized)
    {
        if (!m_pimpl->initialize())
        {
            return false;
        }
    }
    if (m_pimpl->settings.single_threaded && m_pimpl->needUpdate())
    {
        m_pimpl->update();
    }
    if (stick_id >= m_pimpl->sticks_values.size())
    {
        yCError(KEYBOARDJOYPAD) << "The stick with id" << stick_id << "does not exist.";
        return false;
    }

    value.resize(m_pimpl->sticks_values[stick_id].size());

    for (size_t i = 0; i < value.size(); i++)
    {
        value[i] = m_pimpl->sticks_values[stick_id][i];
    }

    if (value.size() != 2)
    {
        return true;
    }

    if (coordinate_mode == JoypadCtrl_coordinateMode::JypCtrlcoord_POLAR)
    {
        double norm = sqrt(value[0] * value[0] + value[1] * value[1]);
        double angle = atan2(value[1], value[0]);
        value[0] = norm;
        value[1] = angle;
    }

    return true;
}

bool yarp::dev::KeyboardJoypad::getTouch(unsigned int /*touch_id*/, yarp::sig::Vector& /*value*/)
{
    yCError(KEYBOARDJOYPAD) << "This device does not consider touch surfaces.";
    return false;
}

bool yarp::dev::KeyboardJoypad::prepareForReconnect()
{
    return true;
}

bool yarp::dev::KeyboardJoypad::consumeDisconnectEvent()
{
    return false;
}
