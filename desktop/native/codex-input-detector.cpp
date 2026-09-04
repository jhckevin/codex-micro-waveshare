#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<std::pair<WORD, bool>> observed;

WORD canonical_key(const KBDLLHOOKSTRUCT& event) {
  const bool extended = (event.flags & LLKHF_EXTENDED) != 0;
  if (event.vkCode == VK_CONTROL || event.vkCode == VK_LCONTROL ||
      event.vkCode == VK_RCONTROL) {
    return extended ? static_cast<WORD>(VK_RCONTROL)
                    : static_cast<WORD>(VK_CONTROL);
  }
  if (event.vkCode == VK_MENU || event.vkCode == VK_LMENU ||
      event.vkCode == VK_RMENU) {
    return extended ? static_cast<WORD>(VK_RMENU)
                    : static_cast<WORD>(VK_MENU);
  }
  return static_cast<WORD>(event.vkCode);
}

LRESULT CALLBACK keyboard_hook(int code, WPARAM message, LPARAM parameter) {
  if (code == HC_ACTION) {
    const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
    if ((event->flags & LLKHF_INJECTED) != 0) {
      const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
      const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
      if (down || up) observed.emplace_back(canonical_key(*event), down);
      return 1;  // CI detector: never allow tested shortcuts to reach the shell.
    }
  }
  return CallNextHookEx(nullptr, code, message, parameter);
}

bool write_all(HANDLE pipe, const std::string& value) {
  DWORD written = 0;
  return WriteFile(pipe, value.data(), static_cast<DWORD>(value.size()),
                   &written, nullptr) != FALSE && written == value.size();
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE input_read{}, input_write{}, output_read{}, output_write{};
  if (!CreatePipe(&input_read, &input_write, &security, 0) ||
      !CreatePipe(&output_read, &output_write, &security, 0)) return 3;
  SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input_read;
  startup.hStdOutput = output_write;
  startup.hStdError = output_write;
  PROCESS_INFORMATION process{};
  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
  if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return 4;
  CloseHandle(input_read);
  CloseHandle(output_write);

  const HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, nullptr, 0);
  if (hook == nullptr) return 5;
  const std::vector<std::string> requests = {
      "{\"id\":1,\"op\":\"key_down\",\"key\":\"RightAlt\"}\n",
      "{\"id\":2,\"op\":\"key_up\",\"key\":\"RightAlt\"}\n",
      "{\"id\":3,\"op\":\"key_down\",\"key\":\"Control\"}\n",
      "{\"id\":4,\"op\":\"key_down\",\"key\":\"C\"}\n",
      "{\"id\":5,\"op\":\"key_up\",\"key\":\"C\"}\n",
      "{\"id\":6,\"op\":\"key_up\",\"key\":\"Control\"}\n",
      "{\"id\":7,\"op\":\"key_down\",\"key\":\"Windows\"}\n",
      "{\"id\":8,\"op\":\"key_down\",\"key\":\"H\"}\n",
      "{\"id\":9,\"op\":\"key_up\",\"key\":\"H\"}\n",
      "{\"id\":10,\"op\":\"key_up\",\"key\":\"Windows\"}\n",
      "{\"id\":11,\"op\":\"key_down\",\"key\":\"Windows\"}\n",
      "{\"id\":12,\"op\":\"key_down\",\"key\":\"Alt\"}\n",
      "{\"id\":13,\"op\":\"key_down\",\"key\":\"Space\"}\n",
      "{\"id\":14,\"op\":\"key_up\",\"key\":\"Space\"}\n",
      "{\"id\":15,\"op\":\"key_up\",\"key\":\"Alt\"}\n",
      "{\"id\":16,\"op\":\"key_up\",\"key\":\"Windows\"}\n"};
  for (const auto& request : requests) if (!write_all(input_write, request)) return 6;

  const ULONGLONG deadline = GetTickCount64() + 3000;
  MSG message{};
  while (observed.size() < requests.size() && GetTickCount64() < deadline) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(2);
  }
  const auto key = [](int code, bool down) {
    return std::pair<WORD, bool>{static_cast<WORD>(code), down};
  };
  const std::vector<std::pair<WORD, bool>> expected = {
      key(VK_RMENU, true), key(VK_RMENU, false),
      key(VK_CONTROL, true), key('C', true), key('C', false),
      key(VK_CONTROL, false), key(VK_LWIN, true), key('H', true),
      key('H', false), key(VK_LWIN, false), key(VK_LWIN, true),
      key(VK_MENU, true), key(VK_SPACE, true), key(VK_SPACE, false),
      key(VK_MENU, false), key(VK_LWIN, false)};

  UnhookWindowsHookEx(hook);
  CloseHandle(input_write);
  WaitForSingleObject(process.hProcess, 2000);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(output_read);
  if (observed != expected) {
    std::fprintf(stderr, "shortcut mismatch expected=%zu observed=%zu\n",
                 expected.size(), observed.size());
    const std::size_t count = (expected.size() > observed.size())
                                  ? expected.size()
                                  : observed.size();
    for (std::size_t index = 0; index < count; ++index) {
      const unsigned expected_key = index < expected.size() ? expected[index].first : 0;
      const int expected_down = index < expected.size() && expected[index].second;
      const unsigned observed_key = index < observed.size() ? observed[index].first : 0;
      const int observed_down = index < observed.size() && observed[index].second;
      std::fprintf(stderr, "event[%zu] expected=%u/%d observed=%u/%d%s\n",
                   index, expected_key, expected_down, observed_key,
                   observed_down,
                   (index < expected.size() && index < observed.size() &&
                    expected[index] == observed[index])
                       ? ""
                       : " mismatch");
    }
    return 7;
  }
  std::printf("{\"ok\":true,\"injected_events\":%zu,\"suppressed\":true}\n",
              observed.size());
  return 0;
}
