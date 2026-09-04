#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <atomic>
#include <cctype>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace {
using winrt::Windows::Devices::Bluetooth::BluetoothCacheMode;
using winrt::Windows::Devices::Bluetooth::BluetoothLEDevice;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using winrt::Windows::Devices::Enumeration::DeviceInformation;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;

std::mutex output_mutex;

void harden_process() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
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
         std::isdigit(static_cast<unsigned char>(json[end]))) {
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
    if (escaped == '"' || escaped == '\\' || escaped == '/') {
      value.push_back(escaped);
    } else {
      return std::nullopt;
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

void closed_event(const std::string& error) {
  std::scoped_lock lock(output_mutex);
  std::cout << "{\"event\":\"closed\",\"error\":\""
            << escape_json(error) << "\"}\n" << std::flush;
}

std::optional<std::vector<std::uint8_t>> base64_decode(
    const std::string& encoded) {
  DWORD size = 0;
  if (!CryptStringToBinaryA(encoded.c_str(),
                            static_cast<DWORD>(encoded.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                            nullptr, &size, nullptr, nullptr)) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(size);
  if (!CryptStringToBinaryA(encoded.c_str(),
                            static_cast<DWORD>(encoded.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                            bytes.data(), &size, nullptr, nullptr)) {
    return std::nullopt;
  }
  bytes.resize(size);
  return bytes;
}

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
  DWORD size = 0;
  CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                       CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                       nullptr, &size);
  std::string encoded(size, '\0');
  if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            encoded.data(), &size)) {
    return {};
  }
  if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
  return encoded;
}

void notification(const std::vector<std::uint8_t>& bytes) {
  const std::string encoded = base64_encode(bytes);
  if (encoded.empty() && !bytes.empty()) return;
  std::scoped_lock lock(output_mutex);
  std::cout << "{\"event\":\"notification\",\"value_base64\":\""
            << encoded << "\"}\n" << std::flush;
}

class GattChannel {
 public:
  bool open(const winrt::guid& service_uuid, const winrt::guid& rx_uuid,
            const winrt::guid& tx_uuid, std::string& error) {
    close();
    try {
      // Windows may retain a bonded HID device while its cached GATT service
      // list predates the companion configuration service. Never open that
      // cached service proxy directly: after a slot switch it can outlive the
      // underlying GATT session. Discover only through the currently connected
      // paired device and force one uncached service lookup.
      const auto paired_selector =
          BluetoothLEDevice::GetDeviceSelectorFromPairingState(true);
      const auto paired = DeviceInformation::FindAllAsync(paired_selector).get();
      for (const auto& information : paired) {
        if (information.Name() != L"Codex Micro") continue;
        auto device = BluetoothLEDevice::FromIdAsync(information.Id()).get();
        if (!device) continue;
        // A remembered slot can remain paired while another slot is active.
        // Do not ask WinRT for uncached services on that disconnected proxy;
        // some Windows builds dereference a stale GATT session internally.
        if (device.ConnectionStatus() !=
            winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus::Connected)
          continue;
        const auto services = device.GetGattServicesForUuidAsync(
            service_uuid, BluetoothCacheMode::Uncached).get();
        if (services.Status() != GattCommunicationStatus::Success) continue;
        for (const auto& candidate : services.Services()) {
          if (!attach(candidate, rx_uuid, tx_uuid)) continue;
          device_ = device;
          return true;
        }
      }
      error = "paired Codex Micro configuration service not found";
      return false;
    } catch (const winrt::hresult_error& failure) {
      error = winrt::to_string(failure.message());
      return false;
    }
  }

  bool write(const std::vector<std::uint8_t>& bytes, std::string& error) {
    if (!open_.load() || bytes.empty() || bytes.size() > 512) {
      error = "BLE link is not open or payload is invalid";
      return false;
    }
    try {
      DataWriter writer;
      writer.WriteBytes(bytes);
      const auto result =
          rx_.WriteValueWithResultAsync(
                 writer.DetachBuffer(), GattWriteOption::WriteWithoutResponse)
              .get();
      if (result.Status() != GattCommunicationStatus::Success) {
        error = "GATT write failed";
        return false;
      }
      return true;
    } catch (const winrt::hresult_error& failure) {
      error = winrt::to_string(failure.message());
      return false;
    }
  }

  void close() {
    if (open_.exchange(false)) {
      try {
        tx_.ValueChanged(value_changed_);
        tx_.WriteClientCharacteristicConfigurationDescriptorAsync(
               GattClientCharacteristicConfigurationDescriptorValue::None)
            .get();
      } catch (...) {
      }
    }
    tx_ = nullptr;
    rx_ = nullptr;
    service_ = nullptr;
    device_ = nullptr;
  }

  ~GattChannel() { close(); }

 private:
  bool attach(const GattDeviceService& candidate, const winrt::guid& rx_uuid,
              const winrt::guid& tx_uuid) {
    const auto rx_result = candidate.GetCharacteristicsForUuidAsync(
        rx_uuid, BluetoothCacheMode::Uncached).get();
    const auto tx_result = candidate.GetCharacteristicsForUuidAsync(
        tx_uuid, BluetoothCacheMode::Uncached).get();
    if (rx_result.Status() != GattCommunicationStatus::Success ||
        tx_result.Status() != GattCommunicationStatus::Success ||
        rx_result.Characteristics().Size() == 0 ||
        tx_result.Characteristics().Size() == 0) {
      return false;
    }
    auto rx_candidate = rx_result.Characteristics().GetAt(0);
    auto tx_candidate = tx_result.Characteristics().GetAt(0);
    const auto ccc =
        tx_candidate.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify)
            .get();
    if (ccc != GattCommunicationStatus::Success) return false;
    service_ = candidate;
    rx_ = rx_candidate;
    tx_ = tx_candidate;
    value_changed_ = tx_.ValueChanged(
        [this](const GattCharacteristic&,
               const GattValueChangedEventArgs& args) {
          try {
            const auto reader =
                DataReader::FromBuffer(args.CharacteristicValue());
            std::vector<std::uint8_t> bytes(reader.UnconsumedBufferLength());
            if (!bytes.empty()) reader.ReadBytes(bytes);
            notification(bytes);
          } catch (...) {
            closed_event("notification read failed");
          }
        });
    open_.store(true);
    return true;
  }

  std::atomic_bool open_{false};
  BluetoothLEDevice device_{nullptr};
  GattDeviceService service_{nullptr};
  GattCharacteristic rx_{nullptr};
  GattCharacteristic tx_{nullptr};
  winrt::event_token value_changed_{};
};

std::optional<winrt::guid> parse_guid(const std::string& value) {
  const std::wstring wide = widen(value);
  if (wide.empty()) return std::nullopt;
  try {
    return winrt::guid(wide);
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace

int main() {
  harden_process();
  SetConsoleOutputCP(CP_UTF8);
  std::ios::sync_with_stdio(false);
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  GattChannel channel;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto id = integer_field(line, "id");
    const auto op = string_field(line, "op");
    if (!id || !op || *id <= 0) {
      if (id) reply(*id, false, "invalid request");
      continue;
    }
    if (*op == "open") {
      const auto service = string_field(line, "service_uuid");
      const auto rx = string_field(line, "rx_uuid");
      const auto tx = string_field(line, "tx_uuid");
      const auto service_guid = service ? parse_guid(*service) : std::nullopt;
      const auto rx_guid = rx ? parse_guid(*rx) : std::nullopt;
      const auto tx_guid = tx ? parse_guid(*tx) : std::nullopt;
      if (!service_guid || !rx_guid || !tx_guid ||
          line.find("\"require_encryption\":true") == std::string::npos) {
        reply(*id, false, "invalid encrypted GATT request");
        continue;
      }
      std::string error;
      const bool ok = channel.open(*service_guid, *rx_guid, *tx_guid, error);
      reply(*id, ok, error);
      continue;
    }
    if (*op == "write") {
      const auto encoded = string_field(line, "value_base64");
      const auto bytes = encoded ? base64_decode(*encoded) : std::nullopt;
      std::string error;
      const bool ok = bytes && channel.write(*bytes, error);
      reply(*id, ok, bytes ? error : "invalid base64 payload");
      continue;
    }
    if (*op == "close") {
      channel.close();
      reply(*id, true);
      break;
    }
    reply(*id, false, "unsupported operation");
  }
  channel.close();
  return 0;
}
