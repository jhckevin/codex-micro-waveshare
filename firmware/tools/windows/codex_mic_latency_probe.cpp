#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};

template <typename T>
void release(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

std::set<DWORD> find_processes(const wchar_t* image_name) {
    std::set<DWORD> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, image_name) == 0) {
                result.insert(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool target_capture_active(IMMDeviceCollection* devices,
                           const std::set<DWORD>& target_pids) {
    UINT device_count = 0;
    if (FAILED(devices->GetCount(&device_count))) {
        return false;
    }

    for (UINT device_index = 0; device_index < device_count; ++device_index) {
        IMMDevice* device = nullptr;
        IAudioSessionManager2* manager = nullptr;
        IAudioSessionEnumerator* sessions = nullptr;
        bool active = false;

        if (SUCCEEDED(devices->Item(device_index, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2),
                                       CLSCTX_ALL,
                                       nullptr,
                                       reinterpret_cast<void**>(&manager))) &&
            SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
            int session_count = 0;
            if (SUCCEEDED(sessions->GetCount(&session_count))) {
                for (int session_index = 0; session_index < session_count; ++session_index) {
                    IAudioSessionControl* control = nullptr;
                    IAudioSessionControl2* control2 = nullptr;
                    AudioSessionState state = AudioSessionStateInactive;
                    DWORD pid = 0;
                    if (SUCCEEDED(sessions->GetSession(session_index, &control)) &&
                        SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                         reinterpret_cast<void**>(&control2))) &&
                        SUCCEEDED(control2->GetProcessId(&pid)) &&
                        target_pids.count(pid) != 0 &&
                        SUCCEEDED(control->GetState(&state)) &&
                        state == AudioSessionStateActive) {
                        active = true;
                    }
                    release(control2);
                    release(control);
                    if (active) {
                        break;
                    }
                }
            }
        }
        release(sessions);
        release(manager);
        release(device);
        if (active) {
            return true;
        }
    }
    return false;
}

bool write_serial(HANDLE serial, const char* command) {
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(std::char_traits<char>::length(command));
    return WriteFile(serial, command, size, &written, nullptr) != FALSE &&
           written == size &&
           FlushFileBuffers(serial) != FALSE;
}

double milliseconds_between(LARGE_INTEGER start,
                            LARGE_INTEGER end,
                            LARGE_INTEGER frequency) {
    return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
           static_cast<double>(frequency.QuadPart);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* port = argc > 1 ? argv[1] : L"COM5";
    const DWORD hold_ms = argc > 2 ? std::max<DWORD>(250, std::wcstoul(argv[2], nullptr, 10))
                                  : 1200;
    const DWORD timeout_ms = argc > 3 ? std::max<DWORD>(500, std::wcstoul(argv[3], nullptr, 10))
                                     : 3000;

    ComScope com;
    if (FAILED(com.result)) {
        std::fprintf(stderr, "COM initialization failed: 0x%08lx\n", com.result);
        return 2;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* capture_devices = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(hr)) {
        hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &capture_devices);
    }
    if (FAILED(hr)) {
        std::fprintf(stderr, "Audio endpoint enumeration failed: 0x%08lx\n", hr);
        release(enumerator);
        return 3;
    }

    const auto target_pids = find_processes(L"ChatGPT.exe");
    if (target_pids.empty()) {
        std::fprintf(stderr, "ChatGPT.exe is not running\n");
        release(capture_devices);
        release(enumerator);
        return 4;
    }
    if (target_capture_active(capture_devices, target_pids)) {
        std::fprintf(stderr, "ChatGPT capture session is already active; stop recording first\n");
        release(capture_devices);
        release(enumerator);
        return 5;
    }

    std::wstring serial_path = L"\\\\.\\";
    serial_path += port;
    HANDLE serial = CreateFileW(serial_path.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (serial == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "Cannot open serial port (error %lu)\n", GetLastError());
        release(capture_devices);
        release(enumerator);
        return 6;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(serial, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(serial, &dcb);

    LARGE_INTEGER frequency{};
    LARGE_INTEGER pressed{};
    LARGE_INTEGER accepted{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&pressed);
    if (!write_serial(serial, "@hil key down mic\n")) {
        std::fprintf(stderr, "MIC press injection failed (error %lu)\n", GetLastError());
        CloseHandle(serial);
        release(capture_devices);
        release(enumerator);
        return 7;
    }

    bool observed = false;
    while (milliseconds_between(pressed, accepted, frequency) < timeout_ms) {
        if (target_capture_active(capture_devices, target_pids)) {
            QueryPerformanceCounter(&accepted);
            observed = true;
            break;
        }
        QueryPerformanceCounter(&accepted);
        Sleep(1);
    }

    const DWORD elapsed_after_press =
        static_cast<DWORD>(milliseconds_between(pressed, accepted, frequency));
    if (elapsed_after_press < hold_ms) {
        Sleep(hold_ms - elapsed_after_press);
    }
    LARGE_INTEGER released{};
    QueryPerformanceCounter(&released);
    const bool release_sent = write_serial(serial, "@hil key up mic\n");
    CloseHandle(serial);

    if (!observed) {
        std::printf(
            "{\"ok\":false,\"reason\":\"codex_capture_not_observed\","
            "\"timeout_ms\":%lu,\"release_sent\":%s}\n",
            timeout_ms,
            release_sent ? "true" : "false");
        release(capture_devices);
        release(enumerator);
        return 8;
    }

    std::printf(
        "{\"ok\":true,\"endpoint\":\"ChatGPT.exe capture active\","
        "\"press_to_codex_capture_ms\":%.3f,\"hold_ms\":%.3f,"
        "\"release_sent\":%s,\"target_pid_count\":%zu}\n",
        milliseconds_between(pressed, accepted, frequency),
        milliseconds_between(pressed, released, frequency),
        release_sent ? "true" : "false",
        target_pids.size());

    release(capture_devices);
    release(enumerator);
    return release_sent ? 0 : 9;
}
