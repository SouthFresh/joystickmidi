// --- Common C++ Headers ---
#include <iostream>
#include <vector>
#include <string>
#include <limits>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <mutex>

// --- Platform-Specific Includes ---
#ifdef _WIN32
    #define UNICODE
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #define _WIN32_WINNT 0x0601
    #include <windows.h>
    #include <hidsdi.h>
    #include <hidpi.h>
    #include <setupapi.h>
    #include <wtypes.h>
#else // Linux
    #include <libudev.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <linux/input.h>
    #include <sys/ioctl.h>
    #include <string.h>
    #include <errno.h>
    #include <poll.h>
    #include <cstdint>
    // Define LONG for Linux to match the Windows type used in shared code
    typedef int32_t LONG;
    #define BITS_PER_LONG (sizeof(long) * 8)
#endif

// --- Project-Specific Headers ---
#include "rtmidi/RtMidi.h"
#include "third_party/nlohmann/json.hpp"

// --- Namespaces and Constants ---
using json = nlohmann::json;
namespace fs = std::filesystem;
const std::string CONFIG_EXTENSION = ".hidmidi.json";

// --- Data Structures ---
struct ControlInfo {
    bool isButton = false;
    LONG logicalMin = 0;
    LONG logicalMax = 0;
    std::string name = "Unknown Control";

#ifdef _WIN32
    USAGE usagePage = 0;
    USAGE usage = 0;
#else // Linux
    uint16_t eventType = 0;
    uint16_t eventCode = 0;
#endif
};

enum class MidiMessageType { NONE, NOTE_ON_OFF, CC };

struct ControlMapping {
    ControlInfo control;
    MidiMessageType midiMessageType = MidiMessageType::NONE;
    int midiChannel = -1;  // -1 means use default channel
    int midiNoteOrCCNumber = 0;
    int midiValueNoteOnVelocity = 64;
    int midiValueCCOn = 127;
    int midiValueCCOff = 0;
    LONG calibrationMinHid = 0;
    LONG calibrationMaxHid = 0;
    bool calibrationDone = false;
    bool reverseAxis = false;
};

struct MidiMappingConfig {
    std::string hidDevicePath;
    std::string hidDeviceName;
    std::string midiDeviceName;
    int defaultMidiChannel = 0;
    int midiSendIntervalMs = 1;
    std::vector<ControlMapping> mappings;
};

// --- JSON Serialization ---
NLOHMANN_JSON_SERIALIZE_ENUM(MidiMessageType, {
    {MidiMessageType::NONE, nullptr},
    {MidiMessageType::NOTE_ON_OFF, "NoteOnOff"},
    {MidiMessageType::CC, "CC"}
})

void to_json(json& j, const ControlInfo& ctrl) {
    j = json{
        {"isButton", ctrl.isButton}, {"logicalMin", ctrl.logicalMin},
        {"logicalMax", ctrl.logicalMax}, {"name", ctrl.name}
    };
#ifdef _WIN32
    j["usagePage"] = ctrl.usagePage;
    j["usage"] = ctrl.usage;
#else
    j["eventType"] = ctrl.eventType;
    j["eventCode"] = ctrl.eventCode;
#endif
}

void from_json(const json& j, ControlInfo& ctrl) {
    j.at("isButton").get_to(ctrl.isButton);
    j.at("logicalMin").get_to(ctrl.logicalMin);
    j.at("logicalMax").get_to(ctrl.logicalMax);
    j.at("name").get_to(ctrl.name);
#ifdef _WIN32
    ctrl.usagePage = j.value("usagePage", 0);
    ctrl.usage = j.value("usage", 0);
#else
    ctrl.eventType = j.value("eventType", 0);
    ctrl.eventCode = j.value("eventCode", 0);
#endif
}

void to_json(json& j, const ControlMapping& mapping) {
    j = json{
        {"control", mapping.control},
        {"midiMessageType", mapping.midiMessageType},
        {"midiChannel", mapping.midiChannel},
        {"midiNoteOrCCNumber", mapping.midiNoteOrCCNumber},
        {"midiValueNoteOnVelocity", mapping.midiValueNoteOnVelocity},
        {"midiValueCCOn", mapping.midiValueCCOn},
        {"midiValueCCOff", mapping.midiValueCCOff},
        {"calibrationMinHid", mapping.calibrationMinHid},
        {"calibrationMaxHid", mapping.calibrationMaxHid},
        {"calibrationDone", mapping.calibrationDone},
        {"reverseAxis", mapping.reverseAxis}
    };
}

void from_json(const json& j, ControlMapping& mapping) {
    j.at("control").get_to(mapping.control);
    j.at("midiMessageType").get_to(mapping.midiMessageType);
    mapping.midiChannel = j.value("midiChannel", -1);
    j.at("midiNoteOrCCNumber").get_to(mapping.midiNoteOrCCNumber);
    mapping.midiValueNoteOnVelocity = j.value("midiValueNoteOnVelocity", 64);
    mapping.midiValueCCOn = j.value("midiValueCCOn", 127);
    mapping.midiValueCCOff = j.value("midiValueCCOff", 0);
    mapping.calibrationMinHid = j.value("calibrationMinHid", 0);
    mapping.calibrationMaxHid = j.value("calibrationMaxHid", 0);
    mapping.calibrationDone = j.value("calibrationDone", false);
    mapping.reverseAxis = j.value("reverseAxis", false);
}

void to_json(json& j, const MidiMappingConfig& cfg) {
    j = json{
        {"hidDevicePath", cfg.hidDevicePath},
        {"hidDeviceName", cfg.hidDeviceName},
        {"midiDeviceName", cfg.midiDeviceName},
        {"defaultMidiChannel", cfg.defaultMidiChannel},
        {"midiSendIntervalMs", cfg.midiSendIntervalMs},
        {"mappings", cfg.mappings}
    };
}

void from_json(const json& j, MidiMappingConfig& cfg) {
    j.at("hidDevicePath").get_to(cfg.hidDevicePath);
    j.at("hidDeviceName").get_to(cfg.hidDeviceName);
    j.at("midiDeviceName").get_to(cfg.midiDeviceName);
    cfg.defaultMidiChannel = j.value("defaultMidiChannel", 0);
    cfg.midiSendIntervalMs = j.value("midiSendIntervalMs", 1);
    j.at("mappings").get_to(cfg.mappings);
}

// --- Global State ---
std::atomic<bool> g_quitFlag(false);
RtMidiOut g_midiOut;
MidiMappingConfig g_currentConfig;
std::thread g_inputThread;
std::mutex g_consoleMutex;

// Per-mapping state tracking
struct MappingState {
    std::atomic<LONG> currentValue{0};
    std::atomic<bool> valueChanged{false};
    LONG previousValue = -1;
    int lastSentMidiValue = -1;

    MappingState() = default;
    MappingState(MappingState&& other) noexcept
        : currentValue(other.currentValue.load()),
          valueChanged(other.valueChanged.load()),
          previousValue(other.previousValue),
          lastSentMidiValue(other.lastSentMidiValue) {}
    MappingState& operator=(MappingState&& other) noexcept {
        currentValue = other.currentValue.load();
        valueChanged = other.valueChanged.load();
        previousValue = other.previousValue;
        lastSentMidiValue = other.lastSentMidiValue;
        return *this;
    }
    MappingState(const MappingState&) = delete;
    MappingState& operator=(const MappingState&) = delete;
};
std::vector<MappingState> g_mappingStates;
std::mutex g_mappingStatesMutex;

// --- Forward Declarations ---
void ClearScreen();
int GetUserSelection(int maxValidChoice, int minValidChoice = 0);
void DisplayMonitoringOutput();
void ClearInputBuffer();
bool string_ends_with(const std::string& str, const std::string& suffix);
bool SaveConfiguration(const MidiMappingConfig& config, const std::string& filename);
bool LoadConfiguration(const std::string& filename, MidiMappingConfig& config);
std::vector<fs::path> ListConfigurations(const std::string& directory);
bool PerformCalibration(size_t mappingIndex);
void ConfigureMappingMidi(ControlMapping& mapping, int defaultChannel);
void InitializeMappingStates();
bool EditConfiguration(std::vector<ControlInfo>& available_controls);

// ===================================================================================
//
// PLATFORM-SPECIFIC IMPLEMENTATIONS
//
// ===================================================================================

#ifdef _WIN32

// --- Windows Data Structures ---
struct HidDeviceInfo {
    HANDLE handle = nullptr;
    std::string name = "Unknown Device";
    std::string path;
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    HIDP_CAPS caps = {};
    RID_DEVICE_INFO rawInfo = {};

    ~HidDeviceInfo() {
        if (preparsedData) HeapFree(GetProcessHeap(), 0, preparsedData);
    }
    HidDeviceInfo(const HidDeviceInfo&) = delete;
    HidDeviceInfo& operator=(const HidDeviceInfo&) = delete;
    HidDeviceInfo(HidDeviceInfo&& other) noexcept
        : handle(other.handle), name(std::move(other.name)), path(std::move(other.path)),
          preparsedData(other.preparsedData), caps(other.caps), rawInfo(other.rawInfo) {
        other.preparsedData = nullptr;
        other.handle = nullptr;
    }
    HidDeviceInfo() = default;
};

// --- Windows Helper Functions ---
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// --- HID Usage Name Mapping ---
// Maps HID Generic Desktop Page (0x01) usage codes to human-readable names
std::string GetHidUsageName(USAGE usagePage, USAGE usage, bool isButton) {
    if (isButton) {
        // Button page (0x09) - just use "Button N"
        return "Button " + std::to_string(usage);
    }

    // Generic Desktop Page (0x01) - common joystick/gamepad axes
    if (usagePage == 0x01) {
        switch (usage) {
            case 0x30: return "X Axis";
            case 0x31: return "Y Axis";
            case 0x32: return "Z Axis";
            case 0x33: return "Rx Axis";
            case 0x34: return "Ry Axis";
            case 0x35: return "Rz Axis";
            case 0x36: return "Slider";
            case 0x37: return "Dial";
            case 0x38: return "Wheel";
            case 0x39: return "Hat Switch";
            case 0x3A: return "Counted Buffer";
            case 0x3B: return "Byte Count";
            case 0x3C: return "Motion Wakeup";
            case 0x3D: return "Start";
            case 0x3E: return "Select";
            case 0x40: return "Vx";
            case 0x41: return "Vy";
            case 0x42: return "Vz";
            case 0x43: return "Vbrx";
            case 0x44: return "Vbry";
            case 0x45: return "Vbrz";
            case 0x46: return "Vno";
            case 0x47: return "Feature Notification";
            case 0x48: return "Resolution Multiplier";
            default: break;
        }
    }

    // Simulation Controls Page (0x02)
    if (usagePage == 0x02) {
        switch (usage) {
            case 0xB0: return "Aileron";
            case 0xB1: return "Aileron Trim";
            case 0xB2: return "Anti-Torque Control";
            case 0xB3: return "Autopilot Enable";
            case 0xB4: return "Chaff Release";
            case 0xB5: return "Collective Control";
            case 0xB6: return "Dive Brake";
            case 0xB7: return "Electronic Countermeasures";
            case 0xB8: return "Elevator";
            case 0xB9: return "Elevator Trim";
            case 0xBA: return "Rudder";
            case 0xBB: return "Throttle";
            case 0xBC: return "Flight Communications";
            case 0xBD: return "Flare Release";
            case 0xBE: return "Landing Gear";
            case 0xBF: return "Toe Brake";
            case 0xC0: return "Trigger";
            case 0xC1: return "Weapons Arm";
            case 0xC2: return "Weapons Select";
            case 0xC3: return "Wing Flaps";
            case 0xC4: return "Accelerator";
            case 0xC5: return "Brake";
            case 0xC6: return "Clutch";
            case 0xC7: return "Shifter";
            case 0xC8: return "Steering";
            case 0xC9: return "Turret Direction";
            case 0xCA: return "Barrel Elevation";
            case 0xCB: return "Dive Plane";
            case 0xCC: return "Ballast";
            case 0xCD: return "Bicycle Crank";
            case 0xCE: return "Handle Bars";
            case 0xCF: return "Front Brake";
            case 0xD0: return "Rear Brake";
            default: break;
        }
    }

    // Fallback: show usage page and code
    if (usagePage != 0x01 && usagePage != 0x09) {
        std::ostringstream oss;
        oss << "Usage(P:0x" << std::hex << std::setfill('0') << std::setw(2) << usagePage
            << ", U:0x" << std::setw(2) << usage << ")";
        return oss.str();
    }

    // Generic fallback for unknown usages on known pages
    return "Axis " + std::to_string(usage);
}

// --- Windows Device/Control Logic ---
std::vector<HidDeviceInfo> EnumerateHidDevices() {
    std::vector<HidDeviceInfo> hidDevices;
    UINT numDevices = 0;
    GetRawInputDeviceList(NULL, &numDevices, sizeof(RAWINPUTDEVICELIST));
    if (numDevices == 0) return hidDevices;

    auto deviceList = std::make_unique<RAWINPUTDEVICELIST[]>(numDevices);
    GetRawInputDeviceList(deviceList.get(), &numDevices, sizeof(RAWINPUTDEVICELIST));

    for (UINT i = 0; i < numDevices; ++i) {
        if (deviceList[i].dwType != RIM_TYPEHID) continue;

        RID_DEVICE_INFO deviceInfo;
        deviceInfo.cbSize = sizeof(RID_DEVICE_INFO);
        UINT size = sizeof(RID_DEVICE_INFO);
        if (GetRawInputDeviceInfo(deviceList[i].hDevice, RIDI_DEVICEINFO, &deviceInfo, &size) == (UINT)-1) continue;

        if (deviceInfo.hid.usUsagePage == 1 && (deviceInfo.hid.usUsage == 4 || deviceInfo.hid.usUsage == 5)) {
            auto info = std::make_unique<HidDeviceInfo>();
            info->handle = deviceList[i].hDevice;
            info->rawInfo = deviceInfo;

            UINT pathSize = 0;
            GetRawInputDeviceInfo(info->handle, RIDI_DEVICENAME, NULL, &pathSize);
            if (pathSize > 1) {
                std::wstring wpath(pathSize, L'\0');
                GetRawInputDeviceInfo(info->handle, RIDI_DEVICENAME, &wpath[0], &pathSize);
                info->path = WStringToString(wpath);
            }

            HANDLE hFile = CreateFileW(StringToWString(info->path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                wchar_t buffer[256] = {0};
                if (HidD_GetProductString(hFile, buffer, sizeof(buffer))) {
                    info->name = WStringToString(buffer);
                }
                CloseHandle(hFile);
            }

            UINT dataSize = 0;
            GetRawInputDeviceInfo(info->handle, RIDI_PREPARSEDDATA, NULL, &dataSize);
            if (dataSize > 0) {
                info->preparsedData = (PHIDP_PREPARSED_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dataSize);
                if (info->preparsedData && GetRawInputDeviceInfo(info->handle, RIDI_PREPARSEDDATA, info->preparsedData, &dataSize) == dataSize) {
                    if (HidP_GetCaps(info->preparsedData, &info->caps) == HIDP_STATUS_SUCCESS) {
                        hidDevices.push_back(std::move(*info));
                    }
                }
            }
        }
    }
    return hidDevices;
}

std::vector<ControlInfo> GetAvailableControls(PHIDP_PREPARSED_DATA pData, HIDP_CAPS& caps) {
    std::vector<ControlInfo> controls;
    // Get Buttons
    if (caps.NumberInputButtonCaps > 0) {
        auto buttonCapsVec = std::make_unique<HIDP_BUTTON_CAPS[]>(caps.NumberInputButtonCaps);
        USHORT capsLength = caps.NumberInputButtonCaps;
        if (HidP_GetButtonCaps(HidP_Input, buttonCapsVec.get(), &capsLength, pData) == HIDP_STATUS_SUCCESS) {
            for (USHORT i = 0; i < capsLength; ++i) {
                const auto& bCaps = buttonCapsVec[i];
                if (bCaps.IsRange) {
                    for (USAGE u = bCaps.Range.UsageMin; u <= bCaps.Range.UsageMax; ++u) {
                        ControlInfo ctrl;
                        ctrl.isButton = true; ctrl.usagePage = bCaps.UsagePage; ctrl.usage = u;
                        ctrl.name = GetHidUsageName(bCaps.UsagePage, u, true);
                        controls.push_back(ctrl);
                    }
                } else {
                    ControlInfo ctrl;
                    ctrl.isButton = true; ctrl.usagePage = bCaps.UsagePage; ctrl.usage = bCaps.NotRange.Usage;
                    ctrl.name = GetHidUsageName(bCaps.UsagePage, bCaps.NotRange.Usage, true);
                    controls.push_back(ctrl);
                }
            }
        }
    }
    // Get Axes/Values
    if (caps.NumberInputValueCaps > 0) {
        auto valueCapsVec = std::make_unique<HIDP_VALUE_CAPS[]>(caps.NumberInputValueCaps);
        USHORT capsLength = caps.NumberInputValueCaps;
        if (HidP_GetValueCaps(HidP_Input, valueCapsVec.get(), &capsLength, pData) == HIDP_STATUS_SUCCESS) {
            for (USHORT i = 0; i < capsLength; ++i) {
                const auto& vCaps = valueCapsVec[i];
                if (vCaps.IsRange) {
                    for (USAGE u = vCaps.Range.UsageMin; u <= vCaps.Range.UsageMax; ++u) {
                        ControlInfo ctrl;
                        ctrl.isButton = false; ctrl.usagePage = vCaps.UsagePage; ctrl.usage = u;
                        ctrl.logicalMin = vCaps.LogicalMin; ctrl.logicalMax = vCaps.LogicalMax;
                        ctrl.name = GetHidUsageName(vCaps.UsagePage, u, false);
                        controls.push_back(ctrl);
                    }
                } else {
                    ControlInfo ctrl;
                    ctrl.isButton = false; ctrl.usagePage = vCaps.UsagePage; ctrl.usage = vCaps.NotRange.Usage;
                    ctrl.logicalMin = vCaps.LogicalMin; ctrl.logicalMax = vCaps.LogicalMax;
                    ctrl.name = GetHidUsageName(vCaps.UsagePage, vCaps.NotRange.Usage, false);
                    controls.push_back(ctrl);
                }
            }
        }
    }
    return controls;
}

// --- Windows Input Monitoring ---
HWND g_messageWindow = nullptr;
RAWINPUTDEVICE g_rid;
PHIDP_PREPARSED_DATA g_preparsedData = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_INPUT) {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        auto lpb = std::make_unique<BYTE[]>(dwSize);
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.get(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) return 0;

        RAWINPUT* raw = (RAWINPUT*)lpb.get();
        if (raw->header.dwType == RIM_TYPEHID && g_preparsedData) {
            // Process all mapped controls
            for (size_t i = 0; i < g_currentConfig.mappings.size() && i < g_mappingStates.size(); ++i) {
                const auto& mapping = g_currentConfig.mappings[i];
                auto& state = g_mappingStates[i];
                ULONG value = 0;

                if (mapping.control.isButton) {
                    USAGE usage = mapping.control.usage;
                    ULONG usageCount = 1;
                    if (HidP_GetUsages(HidP_Input, mapping.control.usagePage, 0, &usage, &usageCount, g_preparsedData, (PCHAR)raw->data.hid.bRawData, raw->data.hid.dwSizeHid) == HIDP_STATUS_SUCCESS) {
                        value = 1;
                    } else {
                        value = 0;
                    }
                } else {
                    HidP_GetUsageValue(HidP_Input, mapping.control.usagePage, 0, mapping.control.usage, &value, g_preparsedData, (PCHAR)raw->data.hid.bRawData, raw->data.hid.dwSizeHid);
                }

                if (static_cast<LONG>(value) != state.currentValue.load()) {
                    state.currentValue = value;
                    state.valueChanged = true;
                }
            }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    if (uMsg == WM_DESTROY) {
        g_quitFlag = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void InputMonitorLoop() {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"JoystickMidiListener";
    if (!RegisterClass(&wc)) return;

    g_messageWindow = CreateWindowEx(0, wc.lpszClassName, L"Listener", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    if (!g_messageWindow) return;

    g_rid.usUsagePage = 1; // Generic Desktop
    g_rid.usUsage = 4;     // Joystick
    g_rid.dwFlags = RIDEV_INPUTSINK;
    g_rid.hwndTarget = g_messageWindow;
    RegisterRawInputDevices(&g_rid, 1, sizeof(g_rid));
    g_rid.usUsage = 5; // Gamepad
    RegisterRawInputDevices(&g_rid, 1, sizeof(g_rid));

    MSG msg;
    while (!g_quitFlag && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_preparsedData) HeapFree(GetProcessHeap(), 0, g_preparsedData);
    if (g_messageWindow) DestroyWindow(g_messageWindow);
    UnregisterClass(L"JoystickMidiListener", GetModuleHandle(NULL));
}

#else // --- Linux Implementation ---

struct HidDeviceInfo {
    std::string name;
    std::string path;
};

std::vector<HidDeviceInfo> EnumerateHidDevices() {
    std::vector<HidDeviceInfo> found_devices;
    struct udev *udev = udev_new();
    if (!udev) return found_devices;

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (!enumerate) {
        udev_unref(udev);
        return found_devices;
    }

    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *dev_list_entry;

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *syspath = udev_list_entry_get_name(dev_list_entry);
        if (!syspath) continue;

        struct udev_device *dev = udev_device_new_from_syspath(udev, syspath);
        if (!dev) continue;

        const char* is_joystick = udev_device_get_property_value(dev, "ID_INPUT_JOYSTICK");
        if (is_joystick && strcmp(is_joystick, "1") == 0) {
            const char* dev_node = udev_device_get_devnode(dev);
            if (dev_node && (std::string(dev_node).find("/dev/input/event") != std::string::npos)) {
                HidDeviceInfo info;
                info.path = dev_node;
                const char* name = udev_device_get_property_value(dev, "ID_MODEL_FROM_DATABASE");
                if (!name) name = udev_device_get_property_value(dev, "NAME");
                info.name = name ? name : "Unnamed Joystick";
                found_devices.push_back(info);
            }
        }
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    return found_devices;
}

std::vector<ControlInfo> GetAvailableControls(const std::string& devicePath) {
    std::vector<ControlInfo> controls;
    int fd = open(devicePath.c_str(), O_RDONLY);
    if (fd < 0) return controls;

    unsigned long ev_bits[EV_MAX / BITS_PER_LONG + 1] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits);

    auto test_bit = [](int bit, const unsigned long* array) {
        return (array[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1;
    };

    if (test_bit(EV_KEY, ev_bits)) {
        unsigned long key_bits[KEY_MAX / BITS_PER_LONG + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
        for (int code = BTN_JOYSTICK; code < KEY_MAX; ++code) {
            if (test_bit(code, key_bits)) {
                ControlInfo ctrl;
                ctrl.isButton = true; ctrl.eventType = EV_KEY; ctrl.eventCode = code;
                ctrl.logicalMin = 0; ctrl.logicalMax = 1;
                ctrl.name = "Button " + std::to_string(code - BTN_JOYSTICK);
                controls.push_back(ctrl);
            }
        }
    }
    if (test_bit(EV_ABS, ev_bits)) {
        unsigned long abs_bits[ABS_MAX / BITS_PER_LONG + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
        for (int code = 0; code < ABS_MAX; ++code) {
            if (test_bit(code, abs_bits)) {
                struct input_absinfo abs_info;
                if (ioctl(fd, EVIOCGABS(code), &abs_info) >= 0) {
                    ControlInfo ctrl;
                    ctrl.isButton = false; ctrl.eventType = EV_ABS; ctrl.eventCode = code;
                    ctrl.logicalMin = abs_info.minimum; ctrl.logicalMax = abs_info.maximum;
                    ctrl.name = "Axis " + std::to_string(code);
                    controls.push_back(ctrl);
                }
            }
        }
    }
    close(fd);
    return controls;
}

void InputMonitorLoop() {
    int fd = open(g_currentConfig.hidDevicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        std::cerr << "\nError: Could not open device " << g_currentConfig.hidDevicePath << " in input thread. " << strerror(errno) << std::endl;
        return;
    }

    struct input_event ev;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!g_quitFlag) {
        int ret = poll(&pfd, 1, 100);
        if (ret < 0 || !(pfd.revents & POLLIN)) continue;

        if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            // Check all mapped controls for this event
            for (size_t i = 0; i < g_currentConfig.mappings.size() && i < g_mappingStates.size(); ++i) {
                const auto& mapping = g_currentConfig.mappings[i];
                auto& state = g_mappingStates[i];

                if (ev.type == mapping.control.eventType && ev.code == mapping.control.eventCode) {
                    if (static_cast<LONG>(ev.value) != state.currentValue.load()) {
                        state.currentValue = ev.value;
                        state.valueChanged = true;
                    }
                }
            }
        }
    }
    close(fd);
    std::cout << "\nInput monitoring thread finished." << std::endl;
}

#endif

// ===================================================================================
//
// CROSS-PLATFORM HELPER FUNCTIONS
//
// ===================================================================================

void ClearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void ClearInputBuffer() {
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

bool string_ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

int GetUserSelection(int maxValidChoice, int minValidChoice) {
    long long choice = -1;
    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) { g_quitFlag = true; return -1; }
        try {
            if (line.empty()) continue;
            size_t processedChars = 0;
            choice = std::stoll(line, &processedChars);
            if (processedChars == line.length() && choice >= minValidChoice && choice <= maxValidChoice) {
                return static_cast<int>(choice);
            } else {
                std::cout << "Invalid input. Please enter a whole number between " << minValidChoice << " and " << maxValidChoice << "." << std::endl;
            }
        } catch (const std::exception&) {
            std::cout << "Invalid input. Please enter a number." << std::endl;
        }
    }
}

void DisplayMonitoringOutput() {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    const int BAR_WIDTH = 20;
    std::stringstream ss;

    // Move cursor to beginning and display all mappings
    ss << "\r";
    for (size_t i = 0; i < g_currentConfig.mappings.size() && i < g_mappingStates.size(); ++i) {
        const auto& mapping = g_currentConfig.mappings[i];
        const auto& state = g_mappingStates[i];

        std::string shortName = mapping.control.name.substr(0, 12);
        ss << "[" << std::left << std::setw(12) << shortName << "] ";

        if (mapping.control.isButton) {
            ss << (state.currentValue.load() ? "ON " : "OFF");
        } else {
            double percentage = 0.0;
            LONG displayRangeMin = mapping.control.logicalMin;
            LONG displayRangeMax = mapping.control.logicalMax;

            if (mapping.calibrationDone) {
                displayRangeMin = mapping.calibrationMinHid;
                displayRangeMax = mapping.calibrationMaxHid;
            }

            LONG displayRange = displayRangeMax - displayRangeMin;
            if (displayRange > 0) {
                LONG clampedValue = std::max(displayRangeMin, std::min(displayRangeMax, state.currentValue.load()));
                percentage = static_cast<double>(clampedValue - displayRangeMin) * 100.0 / static_cast<double>(displayRange);
            } else if (state.currentValue.load() >= displayRangeMax) {
                percentage = 100.0;
            }

            int barLength = static_cast<int>((percentage / 100.0) * BAR_WIDTH + 0.5);
            barLength = std::max(0, std::min(BAR_WIDTH, barLength));

            std::string bar(barLength, '#');
            std::string empty(BAR_WIDTH - barLength, '-');

            ss << "|" << bar << empty << "| " << std::fixed << std::setprecision(0) << std::setw(3) << percentage << "%";
        }
        ss << "  ";
    }

    // Pad with spaces to clear any leftover characters
    std::string outputStr = ss.str();
    outputStr.append(20, ' ');
    std::cout << outputStr << std::flush;
}

bool SaveConfiguration(const MidiMappingConfig& config, const std::string& filename) {
    try {
        json j = config;
        std::ofstream ofs(filename);
        if (!ofs.is_open()) {
            std::cerr << "Error: Could not open file for saving: " << filename << std::endl;
            return false;
        }
        ofs << std::setw(4) << j << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

bool LoadConfiguration(const std::string& filename, MidiMappingConfig& config) {
    try {
        std::ifstream ifs(filename);
        if (!ifs.is_open()) return false;
        json j;
        ifs >> j;
        config = j.get<MidiMappingConfig>();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config '" << filename << "': " << e.what() << std::endl;
        return false;
    }
}

std::vector<fs::path> ListConfigurations(const std::string& directory) {
    std::vector<fs::path> configFiles;
    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && string_ends_with(entry.path().string(), CONFIG_EXTENSION)) {
                configFiles.push_back(entry.path());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing configs: " << e.what() << std::endl;
    }
    std::sort(configFiles.begin(), configFiles.end());
    return configFiles;
}

bool PerformCalibration(size_t mappingIndex) {
    if (mappingIndex >= g_currentConfig.mappings.size() || mappingIndex >= g_mappingStates.size()) {
        return false;
    }

    auto& mapping = g_currentConfig.mappings[mappingIndex];
    auto& state = g_mappingStates[mappingIndex];

    if (mapping.control.isButton) return true;

    auto do_countdown = [](const std::string& stageName) {
        for (int i = 5; i > 0; --i) {
            std::cout << "\rStarting " << stageName << " capture in " << i << " second(s)... " << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "\r" << std::string(50, ' ') << "\r" << std::flush;
    };

    auto capture_hold_value = [&state](bool captureMin) -> LONG {
        LONG extremeValue = captureMin ? std::numeric_limits<LONG>::max() : std::numeric_limits<LONG>::min();
        auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < endTime) {
            auto time_left = std::chrono::duration_cast<std::chrono::seconds>(endTime - std::chrono::steady_clock::now()).count();
            LONG current_val = state.currentValue.load();
            if (captureMin) extremeValue = std::min(extremeValue, current_val);
            else extremeValue = std::max(extremeValue, current_val);

            std::cout << "\rCapturing... HOLD! (" << time_left + 1 << "s) Current: " << current_val << " "
                      << (captureMin ? "Min: " : "Max: ") << extremeValue << "      " << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::cout << std::endl;
        return extremeValue;
    };

    ClearScreen();
    std::cout << "--- Calibrating Axis: " << mapping.control.name << " ---\n\n";
    std::cout << "1. Move the control to its desired MINIMUM position.\n   Get ready!" << std::endl;
    do_countdown("MIN");
    mapping.calibrationMinHid = capture_hold_value(true);
    std::cout << "   Minimum value captured: " << mapping.calibrationMinHid << "\n\n";

    std::cout << "2. Move the control to its desired MAXIMUM position.\n   Get ready!" << std::endl;
    do_countdown("MAX");
    mapping.calibrationMaxHid = capture_hold_value(false);
    std::cout << "   Maximum value captured: " << mapping.calibrationMaxHid << "\n\n";

    if (mapping.calibrationMinHid > mapping.calibrationMaxHid) {
        std::cout << "Note: Min value was greater than Max value. Swapping." << std::endl;
        std::swap(mapping.calibrationMinHid, mapping.calibrationMaxHid);
    }
    mapping.calibrationDone = true;
    std::cout << "Calibration complete. Press Enter to continue." << std::endl;
    ClearInputBuffer();
    std::cin.get();
    return true;
}

// ===================================================================================
//
// HELPER FUNCTIONS FOR MULTI-MAPPING SETUP
//
// ===================================================================================

void InitializeMappingStates() {
    std::lock_guard<std::mutex> lock(g_mappingStatesMutex);
    g_mappingStates.clear();
    g_mappingStates.resize(g_currentConfig.mappings.size());
}

void ConfigureMappingMidi(ControlMapping& mapping, int defaultChannel) {
    std::cout << "\nConfiguring MIDI for: " << mapping.control.name << "\n";

    std::cout << "Select MIDI message type:\n[0] Note On/Off\n[1] CC\n";
    mapping.midiMessageType = (GetUserSelection(1, 0) == 0) ? MidiMessageType::NOTE_ON_OFF : MidiMessageType::CC;

    std::cout << "Use default channel (" << (defaultChannel + 1) << ")? [0] Yes  [1] Custom channel\n";
    if (GetUserSelection(1, 0) == 1) {
        std::cout << "Enter MIDI Channel (1-16): ";
        mapping.midiChannel = GetUserSelection(16, 1) - 1;
    } else {
        mapping.midiChannel = -1;  // Use default
    }

    std::cout << "Enter MIDI Note/CC Number (0-127): ";
    mapping.midiNoteOrCCNumber = GetUserSelection(127, 0);

    if (mapping.midiMessageType == MidiMessageType::NOTE_ON_OFF) {
        std::cout << "Enter Note On Velocity (1-127): ";
        mapping.midiValueNoteOnVelocity = GetUserSelection(127, 1);
    } else {
        if (mapping.control.isButton) {
            std::cout << "Enter CC Value when Pressed (0-127): ";
            mapping.midiValueCCOn = GetUserSelection(127, 0);
            std::cout << "Enter CC Value when Released (0-127): ";
            mapping.midiValueCCOff = GetUserSelection(127, 0);
        } else {
            std::cout << "Reverse MIDI output? (0=No, 1=Yes): ";
            mapping.reverseAxis = (GetUserSelection(1, 0) == 1);
        }
    }
}

int GetEffectiveChannel(const ControlMapping& mapping, int defaultChannel) {
    return (mapping.midiChannel >= 0) ? mapping.midiChannel : defaultChannel;
}

bool EditConfiguration(std::vector<ControlInfo>& available_controls) {
    bool configModified = false;

    while (!g_quitFlag) {
        ClearScreen();
        std::cout << "--- Edit Configuration ---\n";
        std::cout << "Device: " << g_currentConfig.hidDeviceName << "\n";
        std::cout << "Default MIDI Channel: " << (g_currentConfig.defaultMidiChannel + 1) << "\n";
        std::cout << "Current mappings: " << g_currentConfig.mappings.size() << "\n\n";

        // Display current mappings
        if (!g_currentConfig.mappings.empty()) {
            std::cout << "Mapped Controls:\n";
            for (size_t i = 0; i < g_currentConfig.mappings.size(); ++i) {
                const auto& m = g_currentConfig.mappings[i];
                int ch = GetEffectiveChannel(m, g_currentConfig.defaultMidiChannel);
                std::cout << "  " << (i + 1) << ". " << m.control.name
                          << " -> Ch" << (ch + 1) << " "
                          << (m.midiMessageType == MidiMessageType::NOTE_ON_OFF ? "Note" : "CC")
                          << " " << m.midiNoteOrCCNumber << "\n";
            }
            std::cout << "\n";
        }

        std::cout << "Options:\n";
        std::cout << "[0] Continue with current configuration\n";
        std::cout << "[1] Add new control mapping\n";
        if (!g_currentConfig.mappings.empty()) {
            std::cout << "[2] Remove a control mapping\n";
            std::cout << "[3] Edit a control mapping\n";
        }
        std::cout << "[4] Change default MIDI channel\n";
        std::cout << "[5] Save configuration\n";

        int maxOption = g_currentConfig.mappings.empty() ? 5 : 5;
        int choice = GetUserSelection(maxOption, 0);
        if (g_quitFlag) return false;

        switch (choice) {
            case 0: // Continue
                return configModified;

            case 1: { // Add new mapping
                if (available_controls.empty()) {
                    std::cout << "No controls available to add.\n";
                    std::cout << "Press Enter to continue...";
                    std::cin.get();
                    break;
                }

                ClearScreen();
                std::cout << "--- Add Control Mapping ---\n\n";
                std::cout << "Available Controls:\n";
                for (size_t i = 0; i < available_controls.size(); ++i) {
                    bool alreadyMapped = false;
                    for (const auto& m : g_currentConfig.mappings) {
                        #ifdef _WIN32
                        if (m.control.usagePage == available_controls[i].usagePage &&
                            m.control.usage == available_controls[i].usage) {
                            alreadyMapped = true;
                            break;
                        }
                        #else
                        if (m.control.eventType == available_controls[i].eventType &&
                            m.control.eventCode == available_controls[i].eventCode) {
                            alreadyMapped = true;
                            break;
                        }
                        #endif
                    }
                    const auto& ctrl = available_controls[i];
                    std::cout << "[" << std::setw(2) << i << "] " << ctrl.name;
                    if (ctrl.isButton) {
                        std::cout << " (Button)";
                    } else {
                        std::cout << " (Axis/Value: " << ctrl.logicalMin << "-" << ctrl.logicalMax << ")";
                    }
                    std::cout << (alreadyMapped ? " [MAPPED]" : "") << std::endl;
                }
                std::cout << "[" << std::setw(2) << available_controls.size() << "] Cancel\n";

                std::cout << "\nSelect control to add: ";
                int ctrl_choice = GetUserSelection(available_controls.size(), 0);
                if (g_quitFlag) return false;

                if (ctrl_choice < (int)available_controls.size()) {
                    ControlMapping newMapping;
                    newMapping.control = available_controls[ctrl_choice];
                    g_currentConfig.mappings.push_back(newMapping);
                    InitializeMappingStates();

                    size_t mappingIdx = g_currentConfig.mappings.size() - 1;
                    ConfigureMappingMidi(g_currentConfig.mappings[mappingIdx], g_currentConfig.defaultMidiChannel);

                    // Calibrate axis controls
                    if (!newMapping.control.isButton &&
                        g_currentConfig.mappings[mappingIdx].midiMessageType == MidiMessageType::CC) {
                        PerformCalibration(mappingIdx);
                    }
                    configModified = true;
                }
                break;
            }

            case 2: { // Remove mapping
                if (g_currentConfig.mappings.empty()) {
                    std::cout << "No mappings to remove.\n";
                    std::cout << "Press Enter to continue...";
                    std::cin.get();
                    break;
                }

                ClearScreen();
                std::cout << "--- Remove Control Mapping ---\n\n";
                for (size_t i = 0; i < g_currentConfig.mappings.size(); ++i) {
                    const auto& m = g_currentConfig.mappings[i];
                    int ch = GetEffectiveChannel(m, g_currentConfig.defaultMidiChannel);
                    std::cout << "[" << i << "] " << m.control.name
                              << " -> Ch" << (ch + 1) << " "
                              << (m.midiMessageType == MidiMessageType::NOTE_ON_OFF ? "Note" : "CC")
                              << " " << m.midiNoteOrCCNumber << "\n";
                }
                std::cout << "[" << g_currentConfig.mappings.size() << "] Cancel\n";

                std::cout << "\nSelect mapping to remove: ";
                int removeChoice = GetUserSelection(g_currentConfig.mappings.size(), 0);
                if (g_quitFlag) return false;

                if (removeChoice < (int)g_currentConfig.mappings.size()) {
                    std::cout << "Remove '" << g_currentConfig.mappings[removeChoice].control.name << "'? [0] No  [1] Yes\n";
                    if (GetUserSelection(1, 0) == 1) {
                        g_currentConfig.mappings.erase(g_currentConfig.mappings.begin() + removeChoice);
                        InitializeMappingStates();
                        configModified = true;
                        std::cout << "Mapping removed.\n";
                    }
                }
                break;
            }

            case 3: { // Edit mapping
                if (g_currentConfig.mappings.empty()) {
                    std::cout << "No mappings to edit.\n";
                    std::cout << "Press Enter to continue...";
                    std::cin.get();
                    break;
                }

                ClearScreen();
                std::cout << "--- Edit Control Mapping ---\n\n";
                for (size_t i = 0; i < g_currentConfig.mappings.size(); ++i) {
                    const auto& m = g_currentConfig.mappings[i];
                    int ch = GetEffectiveChannel(m, g_currentConfig.defaultMidiChannel);
                    std::cout << "[" << i << "] " << m.control.name
                              << " -> Ch" << (ch + 1) << " "
                              << (m.midiMessageType == MidiMessageType::NOTE_ON_OFF ? "Note" : "CC")
                              << " " << m.midiNoteOrCCNumber << "\n";
                }
                std::cout << "[" << g_currentConfig.mappings.size() << "] Cancel\n";

                std::cout << "\nSelect mapping to edit: ";
                int editChoice = GetUserSelection(g_currentConfig.mappings.size(), 0);
                if (g_quitFlag) return false;

                if (editChoice < (int)g_currentConfig.mappings.size()) {
                    auto& mapping = g_currentConfig.mappings[editChoice];

                    ClearScreen();
                    std::cout << "--- Edit: " << mapping.control.name << " ---\n\n";
                    std::cout << "What would you like to edit?\n";
                    std::cout << "[0] Cancel\n";
                    std::cout << "[1] MIDI settings (type, channel, note/CC number)\n";
                    if (!mapping.control.isButton && mapping.midiMessageType == MidiMessageType::CC) {
                        std::cout << "[2] Recalibrate axis\n";
                        std::cout << "[3] Toggle reverse axis (currently: " << (mapping.reverseAxis ? "Yes" : "No") << ")\n";
                    }

                    int maxEditOption = (!mapping.control.isButton && mapping.midiMessageType == MidiMessageType::CC) ? 3 : 1;
                    int editOption = GetUserSelection(maxEditOption, 0);
                    if (g_quitFlag) return false;

                    switch (editOption) {
                        case 1: // MIDI settings
                            ConfigureMappingMidi(mapping, g_currentConfig.defaultMidiChannel);
                            configModified = true;
                            break;
                        case 2: // Recalibrate
                            if (!mapping.control.isButton && mapping.midiMessageType == MidiMessageType::CC) {
                                PerformCalibration(editChoice);
                                configModified = true;
                            }
                            break;
                        case 3: // Toggle reverse
                            if (!mapping.control.isButton && mapping.midiMessageType == MidiMessageType::CC) {
                                mapping.reverseAxis = !mapping.reverseAxis;
                                std::cout << "Reverse axis: " << (mapping.reverseAxis ? "Yes" : "No") << "\n";
                                configModified = true;
                            }
                            break;
                    }
                }
                break;
            }

            case 4: { // Change default MIDI channel
                ClearScreen();
                std::cout << "--- Change Default MIDI Channel ---\n\n";
                std::cout << "Current default channel: " << (g_currentConfig.defaultMidiChannel + 1) << "\n";
                std::cout << "Enter new default MIDI Channel (1-16): ";
                int newChannel = GetUserSelection(16, 1) - 1;
                if (g_quitFlag) return false;
                g_currentConfig.defaultMidiChannel = newChannel;
                configModified = true;
                std::cout << "Default channel updated to " << (newChannel + 1) << "\n";
                break;
            }

            case 5: { // Save configuration
                ClearScreen();
                std::cout << "--- Save Configuration ---\n\n";
                std::cout << "Enter filename to save (e.g., my_joystick.hidmidi.json): ";
                std::string saveFilename;
                std::getline(std::cin, saveFilename);
                if (!saveFilename.empty()) {
                    if (!string_ends_with(saveFilename, CONFIG_EXTENSION)) {
                        saveFilename += CONFIG_EXTENSION;
                    }
                    if (SaveConfiguration(g_currentConfig, saveFilename)) {
                        std::cout << "Configuration saved to " << saveFilename << "\n";
                        configModified = false;  // Reset since we saved
                    }
                }
                std::cout << "Press Enter to continue...";
                std::cin.get();
                break;
            }
        }
    }

    return configModified;
}

// ===================================================================================
//
// MAIN APPLICATION
//
// ===================================================================================

int main() {
    ClearScreen();
    std::cout << "--- HID to MIDI Mapper (Multi-Control) ---\n\n";
    bool configLoaded = false;

    auto configFiles = ListConfigurations(".");
    if (!configFiles.empty()) {
        std::cout << "Found existing configurations:\n";
        for (size_t i = 0; i < configFiles.size(); ++i) {
            std::cout << "[" << i << "] " << configFiles[i].filename().string() << std::endl;
        }
        std::cout << "[" << configFiles.size() << "] Create New Configuration\n";
        int choice = GetUserSelection(configFiles.size(), 0);
        if (g_quitFlag) return 1;

        if (choice < (int)configFiles.size()) {
            if (LoadConfiguration(configFiles[choice].string(), g_currentConfig)) {
                std::cout << "Configuration loaded successfully with " << g_currentConfig.mappings.size() << " mapping(s)." << std::endl;
                configLoaded = true;
            } else {
                std::cerr << "Failed to load configuration. Starting new setup." << std::endl;
            }
        }
    }

    #ifdef _WIN32
    std::vector<HidDeviceInfo> available_devices;
    #endif
    std::vector<ControlInfo> available_controls;

    if (!configLoaded) {
        ClearScreen();
        std::cout << "--- Step 1: Select HID Controller ---\n";
        #ifdef _WIN32
        available_devices = EnumerateHidDevices();
        if (available_devices.empty()) {
            std::cerr << "No joysticks found." << std::endl; return 1;
        }

        std::cout << "Available Controllers:\n";
        for (size_t i = 0; i < available_devices.size(); ++i) {
            std::cout << "[" << i << "] " << available_devices[i].name << " (" << available_devices[i].path << ")" << std::endl;
        }
        int dev_choice = GetUserSelection(available_devices.size() - 1, 0);
        if (g_quitFlag) return 1;

        g_currentConfig.hidDeviceName = available_devices[dev_choice].name;
        g_currentConfig.hidDevicePath = available_devices[dev_choice].path;

        // On Windows, we need the preparsed data from the selected device
        UINT dataSize = 0;
        GetRawInputDeviceInfo(available_devices[dev_choice].handle, RIDI_PREPARSEDDATA, NULL, &dataSize);
        if (dataSize > 0) {
            g_preparsedData = (PHIDP_PREPARSED_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dataSize);
            GetRawInputDeviceInfo(available_devices[dev_choice].handle, RIDI_PREPARSEDDATA, g_preparsedData, &dataSize);
        }
        available_controls = GetAvailableControls(g_preparsedData, available_devices[dev_choice].caps);
        #else
        auto available_devices = EnumerateHidDevices();
        if (available_devices.empty()) {
            std::cerr << "No joysticks found." << std::endl; return 1;
        }

        std::cout << "Available Controllers:\n";
        for (size_t i = 0; i < available_devices.size(); ++i) {
            std::cout << "[" << i << "] " << available_devices[i].name << " (" << available_devices[i].path << ")" << std::endl;
        }
        int dev_choice = GetUserSelection(available_devices.size() - 1, 0);
        if (g_quitFlag) return 1;

        g_currentConfig.hidDeviceName = available_devices[dev_choice].name;
        g_currentConfig.hidDevicePath = available_devices[dev_choice].path;
        available_controls = GetAvailableControls(g_currentConfig.hidDevicePath);
        #endif

        if (available_controls.empty()) {
            std::cerr << "No usable controls found on this device." << std::endl; return 1;
        }

        ClearScreen();
        std::cout << "--- Step 2: Select MIDI Output ---\n";
        unsigned int portCount = g_midiOut.getPortCount();
        if (portCount == 0) {
            std::cerr << "No MIDI output ports available." << std::endl; return 1;
        }
        for (unsigned int i = 0; i < portCount; ++i) {
            std::cout << "  [" << i << "]: " << g_midiOut.getPortName(i) << std::endl;
        }
        int midi_choice = GetUserSelection(portCount - 1, 0);
        g_midiOut.openPort(midi_choice);
        g_currentConfig.midiDeviceName = g_midiOut.getPortName(midi_choice);

        ClearScreen();
        std::cout << "--- Step 3: Set Default MIDI Channel ---\n";
        std::cout << "Enter default MIDI Channel (1-16): ";
        g_currentConfig.defaultMidiChannel = GetUserSelection(16, 1) - 1;

        // Loop to add multiple controls
        bool addMoreControls = true;
        while (addMoreControls && !g_quitFlag) {
            ClearScreen();
            std::cout << "--- Step 4: Add Control Mapping ---\n";
            std::cout << "Current mappings: " << g_currentConfig.mappings.size() << "\n\n";

            std::cout << "Available Controls:\n";
            for (size_t i = 0; i < available_controls.size(); ++i) {
                // Mark already mapped controls
                bool alreadyMapped = false;
                for (const auto& m : g_currentConfig.mappings) {
                    #ifdef _WIN32
                    if (m.control.usagePage == available_controls[i].usagePage &&
                        m.control.usage == available_controls[i].usage) {
                        alreadyMapped = true;
                        break;
                    }
                    #else
                    if (m.control.eventType == available_controls[i].eventType &&
                        m.control.eventCode == available_controls[i].eventCode) {
                        alreadyMapped = true;
                        break;
                    }
                    #endif
                }
                const auto& ctrl = available_controls[i];
                std::cout << "[" << std::setw(2) << i << "] " << ctrl.name;
                if (ctrl.isButton) {
                    std::cout << " (Button)";
                } else {
                    std::cout << " (Axis/Value: " << ctrl.logicalMin << "-" << ctrl.logicalMax << ")";
                }
                std::cout << (alreadyMapped ? " [MAPPED]" : "") << std::endl;
            }

            std::cout << "\nSelect control to map (or " << available_controls.size() << " to finish adding): ";
            int ctrl_choice = GetUserSelection(available_controls.size(), 0);
            if (g_quitFlag) return 1;

            if (ctrl_choice == (int)available_controls.size()) {
                addMoreControls = false;
            } else {
                ControlMapping newMapping;
                newMapping.control = available_controls[ctrl_choice];

                // Initialize mapping states so calibration can work
                g_currentConfig.mappings.push_back(newMapping);
                InitializeMappingStates();

                // Start input thread if not already running (needed for calibration)
                if (!g_inputThread.joinable()) {
                    g_inputThread = std::thread(InputMonitorLoop);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Let thread start
                }

                size_t mappingIdx = g_currentConfig.mappings.size() - 1;
                ConfigureMappingMidi(g_currentConfig.mappings[mappingIdx], g_currentConfig.defaultMidiChannel);

                // Calibrate axis controls
                if (!newMapping.control.isButton &&
                    g_currentConfig.mappings[mappingIdx].midiMessageType == MidiMessageType::CC) {
                    PerformCalibration(mappingIdx);
                }

                std::cout << "\nAdd another control? [0] Yes  [1] No\n";
                addMoreControls = (GetUserSelection(1, 0) == 0);
            }
        }

        if (g_currentConfig.mappings.empty()) {
            std::cerr << "No controls mapped. Exiting." << std::endl;
            g_quitFlag = true;
            if (g_inputThread.joinable()) g_inputThread.join();
            return 1;
        }
    } else { // Config was loaded
        #ifdef _WIN32
            // On Windows, we need to find the device and get its preparsed data
            bool found = false;
            while (!found && !g_quitFlag) {
                auto devices = EnumerateHidDevices();
                for(auto& dev : devices) {
                    if (dev.path == g_currentConfig.hidDevicePath) {
                        g_preparsedData = dev.preparsedData;
                        dev.preparsedData = nullptr; // Prevent destructor from freeing it
                        available_controls = GetAvailableControls(g_preparsedData, dev.caps);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ClearScreen();
                    std::cout << "--- Device Not Connected ---\n\n";
                    std::cout << "The configured device was not found:\n";
                    std::cout << "  " << g_currentConfig.hidDeviceName << "\n\n";
                    std::cout << "Please connect the device and try again.\n\n";
                    std::cout << "[0] Retry\n[1] Exit\n";
                    int retryChoice = GetUserSelection(1, 0);
                    if (g_quitFlag || retryChoice == 1) {
                        return 1;
                    }
                }
            }
        #else
            // On Linux, check if device exists and get available controls
            bool found = false;
            while (!found && !g_quitFlag) {
                if (fs::exists(g_currentConfig.hidDevicePath)) {
                    available_controls = GetAvailableControls(g_currentConfig.hidDevicePath);
                    if (!available_controls.empty()) {
                        found = true;
                    }
                }
                if (!found) {
                    ClearScreen();
                    std::cout << "--- Device Not Connected ---\n\n";
                    std::cout << "The configured device was not found:\n";
                    std::cout << "  " << g_currentConfig.hidDeviceName << "\n";
                    std::cout << "  (" << g_currentConfig.hidDevicePath << ")\n\n";
                    std::cout << "Please connect the device and try again.\n\n";
                    std::cout << "[0] Retry\n[1] Exit\n";
                    int retryChoice = GetUserSelection(1, 0);
                    if (g_quitFlag || retryChoice == 1) {
                        return 1;
                    }
                }
            }
        #endif

        // Ask if user wants to edit the configuration
        std::cout << "\nOptions:\n[0] Run with current configuration\n[1] Edit configuration\n";
        int editChoice = GetUserSelection(1, 0);
        if (g_quitFlag) return 1;

        // Initialize mapping states and start input thread
        InitializeMappingStates();
        g_inputThread = std::thread(InputMonitorLoop);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Let thread start

        if (editChoice == 1) {
            bool modified = EditConfiguration(available_controls);

            if (g_currentConfig.mappings.empty()) {
                std::cerr << "No controls mapped. Exiting." << std::endl;
                g_quitFlag = true;
                if (g_inputThread.joinable()) g_inputThread.join();
                return 1;
            }

            // Prompt to save if modified
            if (modified) {
                std::cout << "\nConfiguration was modified. Save changes? [0] No  [1] Yes\n";
                if (GetUserSelection(1, 0) == 1) {
                    std::cout << "Enter filename to save (e.g., my_joystick.hidmidi.json): ";
                    std::string saveFilename;
                    std::getline(std::cin, saveFilename);
                    if (!saveFilename.empty()) {
                        if (!string_ends_with(saveFilename, CONFIG_EXTENSION)) {
                            saveFilename += CONFIG_EXTENSION;
                        }
                        if (SaveConfiguration(g_currentConfig, saveFilename)) {
                            std::cout << "Configuration saved to " << saveFilename << std::endl;
                        }
                    }
                }
            }
        }

        unsigned int portCount = g_midiOut.getPortCount();
        int midi_port = -1;
        for (unsigned int i = 0; i < portCount; ++i) {
            if (g_midiOut.getPortName(i) == g_currentConfig.midiDeviceName) {
                midi_port = i;
                break;
            }
        }
        if (midi_port == -1) {
            std::cerr << "Configured MIDI port '" << g_currentConfig.midiDeviceName << "' not found." << std::endl;
            g_quitFlag = true;
            if (g_inputThread.joinable()) g_inputThread.join();
            return 1;
        }
        g_midiOut.openPort(midi_port);
    }

    if (!configLoaded) {
        ClearScreen();
        std::cout << "--- Step 5: Save Configuration ---\n";
        std::cout << "Configured " << g_currentConfig.mappings.size() << " control mapping(s).\n";
        std::cout << "Enter filename to save (e.g., my_joystick.hidmidi.json), or leave blank to skip: ";
        std::string saveFilename;
        std::getline(std::cin, saveFilename);
        if (!saveFilename.empty()) {
            if (!string_ends_with(saveFilename, CONFIG_EXTENSION)) {
                saveFilename += CONFIG_EXTENSION;
            }
            if (SaveConfiguration(g_currentConfig, saveFilename)) {
                std::cout << "Configuration saved to " << saveFilename << std::endl;
            }
        }
    }

    ClearScreen();
    std::cout << "--- Monitoring Active ---\n";
    std::cout << "Device: " << g_currentConfig.hidDeviceName << std::endl;
    std::cout << "Mappings: " << g_currentConfig.mappings.size() << std::endl;
    for (size_t i = 0; i < g_currentConfig.mappings.size(); ++i) {
        const auto& m = g_currentConfig.mappings[i];
        int ch = GetEffectiveChannel(m, g_currentConfig.defaultMidiChannel);
        std::cout << "  " << (i+1) << ". " << m.control.name << " -> Ch" << (ch+1)
                  << " " << (m.midiMessageType == MidiMessageType::NOTE_ON_OFF ? "Note" : "CC")
                  << " " << m.midiNoteOrCCNumber << std::endl;
    }
    std::cout << "MIDI Port: " << g_currentConfig.midiDeviceName << std::endl;
    std::cout << "(Press Enter to exit on Linux, or close window)\n\n";

    auto lastDisplayTime = std::chrono::steady_clock::now();
    while (!g_quitFlag) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastDisplayTime > std::chrono::milliseconds(1000 / 60)) {
            DisplayMonitoringOutput();
            lastDisplayTime = now;
        }

        // Process all mappings
        for (size_t i = 0; i < g_currentConfig.mappings.size() && i < g_mappingStates.size(); ++i) {
            auto& mapping = g_currentConfig.mappings[i];
            auto& state = g_mappingStates[i];

            if (state.valueChanged.exchange(false)) {
                std::vector<unsigned char> message;
                int channel = GetEffectiveChannel(mapping, g_currentConfig.defaultMidiChannel);

                if (mapping.control.isButton) {
                    bool pressed = state.currentValue.load() != 0;
                    if (pressed != (state.previousValue != 0)) {
                        if (mapping.midiMessageType == MidiMessageType::NOTE_ON_OFF) {
                            message = {(unsigned char)((pressed ? 0x90 : 0x80) | channel),
                                       (unsigned char)mapping.midiNoteOrCCNumber,
                                       (unsigned char)(pressed ? mapping.midiValueNoteOnVelocity : 0)};
                        } else {
                            message = {(unsigned char)(0xB0 | channel),
                                       (unsigned char)mapping.midiNoteOrCCNumber,
                                       (unsigned char)(pressed ? mapping.midiValueCCOn : mapping.midiValueCCOff)};
                        }
                        if (!message.empty()) g_midiOut.sendMessage(&message);
                    }
                } else { // Axis
                    if (mapping.calibrationDone) {
                        LONG range = mapping.calibrationMaxHid - mapping.calibrationMinHid;
                        if (range > 0) {
                            LONG clamped = std::max(mapping.calibrationMinHid,
                                                    std::min(mapping.calibrationMaxHid, state.currentValue.load()));
                            double norm = (double)(clamped - mapping.calibrationMinHid) / range;
                            if (mapping.reverseAxis) norm = 1.0 - norm;
                            int midiVal = (int)(norm * 127.0 + 0.5);
                            if (midiVal != state.lastSentMidiValue) {
                                message = {(unsigned char)(0xB0 | channel),
                                           (unsigned char)mapping.midiNoteOrCCNumber,
                                           (unsigned char)midiVal};
                                g_midiOut.sendMessage(&message);
                                state.lastSentMidiValue = midiVal;
                            }
                        }
                    }
                }
                state.previousValue = state.currentValue.load();
            }
        }

        #ifndef _WIN32
        {
            std::lock_guard<std::mutex> lock(g_consoleMutex);
            struct timeval tv = {0L, 0L};
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(0, &fds);
            if (select(1, &fds, NULL, NULL, &tv) > 0) {
                g_quitFlag = true;
            }
        }
        #endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "\n\nExiting..." << std::endl;
    if (g_inputThread.joinable()) g_inputThread.join();
    if (g_midiOut.isPortOpen()) g_midiOut.closePort();
    return 0;
}
