#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace {

std::mutex output_mutex;

void harden_process() {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
  PROCESS_MITIGATION_DEP_POLICY dep{};
  dep.Enable = 1;
  dep.Permanent = 1;
  SetProcessMitigationPolicy(ProcessDEPPolicy, &dep, sizeof(dep));
  PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensions{};
  extensions.DisableExtensionPoints = 1;
  SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &extensions,
                             sizeof(extensions));
}

std::optional<long long> integer_field(const std::string& json,
                                       const std::string& name) {
  const std::string key = "\"" + name + "\":";
  std::size_t at = json.find(key);
  if (at == std::string::npos) return std::nullopt;
  at += key.size();
  std::size_t end = at;
  while (end < json.size() &&
         (std::isdigit(static_cast<unsigned char>(json[end])) ||
          json[end] == '-')) {
    ++end;
  }
  if (end == at) return std::nullopt;
  try {
    return std::stoll(json.substr(at, end - at));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> string_field(const std::string& json,
                                        const std::string& name) {
  const std::string key = "\"" + name + "\":\"";
  std::size_t at = json.find(key);
  if (at == std::string::npos) return std::nullopt;
  at += key.size();
  std::string value;
  while (at < json.size()) {
    const char ch = json[at++];
    if (ch == '"') return value;
    if (ch != '\\') {
      if (static_cast<unsigned char>(ch) < 0x20) return std::nullopt;
      value.push_back(ch);
      continue;
    }
    if (at >= json.size()) return std::nullopt;
    const char escaped = json[at++];
    switch (escaped) {
      case '"': value.push_back('"'); break;
      case '\\': value.push_back('\\'); break;
      case '/': value.push_back('/'); break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      default: return std::nullopt;
    }
  }
  return std::nullopt;
}

std::wstring widen(const std::string& value) {
  if (value.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        value.data(),
                                        static_cast<int>(value.size()),
                                        nullptr, 0);
  if (count <= 0) return {};
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(),
                          count) != count) {
    return {};
  }
  return result;
}

std::string escape_json(const std::string& value) {
  std::string result;
  for (char ch : value) {
    if (ch == '"' || ch == '\\') result.push_back('\\');
    if (static_cast<unsigned char>(ch) >= 0x20) result.push_back(ch);
  }
  return result;
}

void reply(long long id, bool ok, const std::string& error = {}) {
  std::scoped_lock lock(output_mutex);
  std::cout << "{\"id\":" << id << ",\"ok\":" << (ok ? "true" : "false");
  if (!ok) std::cout << ",\"error\":\"" << escape_json(error) << "\"";
  std::cout << "}\n" << std::flush;
}

std::optional<WORD> virtual_key(const std::string& key) {
  static const std::map<std::string, int> fixed = {
      {"Alt", VK_MENU}, {"RightAlt", VK_RMENU}, {"Control", VK_CONTROL},
      {"Shift", VK_SHIFT}, {"Windows", VK_LWIN}, {"Enter", VK_RETURN},
      {"Escape", VK_ESCAPE}, {"Space", VK_SPACE}, {"Tab", VK_TAB},
      {"Backspace", VK_BACK}, {"Delete", VK_DELETE}, {"Insert", VK_INSERT},
      {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR},
      {"PageDown", VK_NEXT}, {"ArrowUp", VK_UP}, {"ArrowDown", VK_DOWN},
      {"ArrowLeft", VK_LEFT}, {"ArrowRight", VK_RIGHT},
  };
  if (const auto found = fixed.find(key); found != fixed.end()) {
    return static_cast<WORD>(found->second);
  }
  if (key.size() == 1 &&
      ((key[0] >= 'A' && key[0] <= 'Z') ||
       (key[0] >= '0' && key[0] <= '9'))) {
    return static_cast<WORD>(key[0]);
  }
  if (key.size() >= 2 && key[0] == 'F') {
    try {
      const int number = std::stoi(key.substr(1));
      if (number >= 1 && number <= 24) {
        return static_cast<WORD>(VK_F1 + number - 1);
      }
    } catch (...) {
    }
  }
  return std::nullopt;
}

bool send_key(const std::string& key, bool down) {
  const auto vk = virtual_key(key);
  if (!vk) return false;
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = *vk;
  if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
  if (*vk == VK_RMENU || *vk == VK_INSERT || *vk == VK_DELETE ||
      *vk == VK_HOME || *vk == VK_END || *vk == VK_PRIOR ||
      *vk == VK_NEXT || *vk == VK_UP || *vk == VK_DOWN ||
      *vk == VK_LEFT || *vk == VK_RIGHT) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  return SendInput(1, &input, sizeof(input)) == 1;
}

struct FindWindowContext {
  std::vector<std::wstring> needles;
  HWND result{};
};

BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
  auto& context = *reinterpret_cast<FindWindowContext*>(parameter);
  if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
    return TRUE;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               process_id);
  if (process == nullptr) return TRUE;
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  const bool obtained =
      QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
  CloseHandle(process);
  if (!obtained) return TRUE;
  path.resize(size);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
  for (const auto& needle : context.needles) {
    if (path.ends_with(needle)) {
      context.result = window;
      return FALSE;
    }
  }
  return TRUE;
}

bool activate(HWND window) {
  if (window == nullptr) return false;
  if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
  const DWORD foreground_thread =
      GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  const DWORD target_thread = GetWindowThreadProcessId(window, nullptr);
  const DWORD current_thread = GetCurrentThreadId();
  if (foreground_thread != 0) {
    AttachThreadInput(current_thread, foreground_thread, TRUE);
  }
  if (target_thread != 0 && target_thread != foreground_thread) {
    AttachThreadInput(current_thread, target_thread, TRUE);
  }
  BringWindowToTop(window);
  const bool success = SetForegroundWindow(window) != FALSE;
  if (target_thread != 0 && target_thread != foreground_thread) {
    AttachThreadInput(current_thread, target_thread, FALSE);
  }
  if (foreground_thread != 0) {
    AttachThreadInput(current_thread, foreground_thread, FALSE);
  }
  return success;
}

bool focus_needles(std::vector<std::wstring> needles) {
  FindWindowContext context{std::move(needles), nullptr};
  EnumWindows(find_window, reinterpret_cast<LPARAM>(&context));
  return activate(context.result);
}

bool focus_or_launch(const std::wstring& executable) {
  const std::size_t separator = executable.find_last_of(L"\\/");
  if (separator == std::wstring::npos) return false;
  std::wstring name = executable.substr(separator);
  std::transform(name.begin(), name.end(), name.begin(), towlower);
  if (focus_needles({name})) return true;
  std::wstring command = L"\"" + executable + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                      &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace

int main() {
  harden_process();
  SetConsoleOutputCP(CP_UTF8);
  std::ios::sync_with_stdio(false);
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto id = integer_field(line, "id");
    const auto op = string_field(line, "op");
    if (!id || !op || *id <= 0) {
      if (id) reply(*id, false, "invalid request");
      continue;
    }
    if (*op == "key_down" || *op == "key_up") {
      const auto key = string_field(line, "key");
      reply(*id, key && send_key(*key, *op == "key_down"),
            key ? "SendInput failed" : "invalid key");
      continue;
    }
    if (*op == "focus_builtin") {
      const auto app = string_field(line, "app");
      bool ok = false;
      if (app && *app == "codex") {
        ok = focus_needles({L"\\codex.exe"});
      } else if (app && *app == "chatgpt") {
        ok = focus_needles({L"\\chatgpt.exe"});
      }
      reply(*id, ok, "application window not found");
      continue;
    }
    if (*op == "focus_or_launch") {
      const auto executable = string_field(line, "executable");
      const std::wstring path = executable ? widen(*executable) : L"";
      reply(*id, !path.empty() && focus_or_launch(path),
            "application could not be focused or launched");
      continue;
    }
    reply(*id, false, "unsupported operation");
  }
  return 0;
}
