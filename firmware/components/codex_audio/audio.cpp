#include "codex/audio.h"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "switch_samples_generated.h"

#include <atomic>

namespace codex {
namespace {

constexpr char kTag[] = "codex_audio";
constexpr unsigned int kMixFrames = 128;
constexpr unsigned int kMaxVoices = 8;

enum class AudioRequestType : unsigned char {
  Play,
  Enable,
  Disable,
  Volume,
};

struct AudioRequest {
  AudioRequestType type{AudioRequestType::Play};
  SoundProfile profile{SoundProfile::Linear};
  bool down{true};
  bool capacitive{false};
  unsigned char variation{0};
  unsigned char volume{100};
};

struct Voice {
  const short* samples{};
  unsigned int count{};
  unsigned int position{};
  unsigned int sequence{};
  unsigned char gain_percent{100};
  bool capacitive{};
};

QueueHandle_t queue{};
esp_codec_dev_handle_t speaker{};
unsigned int dropped_requests{};
unsigned int play_sequence{};
std::atomic<unsigned int> request_sequence{};

esp_codec_dev_sample_info_t speaker_format() {
  return {
      .bits_per_sample = 16,
      .channel = 1,
      .channel_mask = 0,
      .sample_rate = kSwitchSampleRate,
      .mclk_multiple = 0,
  };
}

bool any_voice(const Voice voices[kMaxVoices]) {
  for (unsigned int index = 0; index < kMaxVoices; ++index) {
    if (voices[index].samples != nullptr) return true;
  }
  return false;
}

void clear_voices(Voice voices[kMaxVoices]) {
  for (unsigned int index = 0; index < kMaxVoices; ++index) {
    voices[index] = {};
  }
}

void start_voice(Voice voices[kMaxVoices], const short* samples,
                 unsigned int count, unsigned int sequence,
                 unsigned char gain_percent = 100,
                 bool capacitive = false) {
  if (samples == nullptr || count == 0) return;
  unsigned int selected = 0;
  bool found_empty = false;
  if (capacitive) {
    for (unsigned int index = 0; index < kMaxVoices; ++index) {
      if (voices[index].capacitive) {
        selected = index;
        found_empty = true;
        break;
      }
    }
  }
  for (unsigned int index = 0; !found_empty && index < kMaxVoices; ++index) {
    if (voices[index].samples == nullptr) {
      selected = index;
      found_empty = true;
      break;
    }
    if (voices[index].sequence < voices[selected].sequence) selected = index;
  }
  if (!found_empty) {
    ESP_LOGD(kTag, "voice pool full; replacing oldest voice");
  }
  voices[selected] = {
      .samples = samples,
      .count = count,
      .position = 0,
      .sequence = sequence,
      .gain_percent = gain_percent,
      .capacitive = capacitive,
  };
}

void process_request(const AudioRequest& request,
                     Voice voices[kMaxVoices],
                     const short* clicky_down,
                     const short* capacitive,
                     bool& enabled, bool& opened,
                     unsigned char& volume,
                     esp_codec_dev_sample_info_t& format) {
  if (request.type == AudioRequestType::Volume) {
    volume = request.volume;
    return;
  }
  if (request.type == AudioRequestType::Enable) {
    if (opened) {
      if (bsp_audio_stream_resume() != ESP_OK) {
        ESP_LOGE(kTag, "I2S resume failed");
      }
      if (esp_codec_dev_set_out_mute(speaker, false) != 0) {
        ESP_LOGE(kTag, "ES8311 unmute failed");
      }
    }
    enabled = opened;
    return;
  }
  if (request.type == AudioRequestType::Disable) {
    enabled = false;
    clear_voices(voices);
    if (opened) {
      if (esp_codec_dev_set_out_mute(speaker, true) != 0) {
        ESP_LOGE(kTag, "ES8311 mute failed");
      }
      if (bsp_audio_stream_suspend() != ESP_OK) {
        ESP_LOGE(kTag, "I2S suspend failed");
      }
    }
    return;
  }
  if (!enabled || !opened) return;

  if (request.capacitive) {
    start_voice(voices, capacitive, kClickSampleCount, ++play_sequence,
                kCapacitiveGainPercent, true);
    return;
  }
  if (request.profile == SoundProfile::Clicky) {
    if (request.down) {
      start_voice(voices, clicky_down, kClickSampleCount, ++play_sequence);
    } else {
      const SwitchSample sample =
          switch_sample(true, false, request.variation);
      start_voice(voices, sample.samples, sample.count, ++play_sequence);
    }
    return;
  }
  const SwitchSample sample =
      switch_sample(request.profile == SoundProfile::Tactile, request.down,
                    request.variation);
  start_voice(voices, sample.samples, sample.count, ++play_sequence);
}

void mix_and_write(Voice voices[kMaxVoices], unsigned char volume,
                   short output[kMixFrames]) {
  unsigned int active = 0;
  for (unsigned int voice = 0; voice < kMaxVoices; ++voice) {
    if (voices[voice].samples != nullptr) ++active;
  }
  const int divisor = active > 1 ? 1 + static_cast<int>(active - 1) / 3 : 1;
  for (unsigned int frame = 0; frame < kMixFrames; ++frame) {
    int mixed = 0;
    for (unsigned int voice = 0; voice < kMaxVoices; ++voice) {
      Voice& item = voices[voice];
      if (item.samples == nullptr) continue;
      if (item.position < item.count) {
        mixed += static_cast<int>(item.samples[item.position++]) *
                 item.gain_percent / 100;
      }
      if (item.position >= item.count) item = {};
    }
    mixed = mixed * static_cast<int>(volume) / (100 * divisor);
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;
    output[frame] = static_cast<short>(mixed);
  }
  const int result =
      esp_codec_dev_write(speaker, output, kMixFrames * sizeof(short));
  if (result != 0) ESP_LOGE(kTag, "speaker write failed: %d", result);
}

void audio_task(void*) {
  speaker = bsp_audio_codec_speaker_init();
  if (speaker == nullptr) {
    ESP_LOGE(kTag, "ES8311 speaker init failed");
    vTaskDelete(nullptr);
    return;
  }
  esp_codec_dev_sample_info_t format = speaker_format();
  esp_codec_dev_set_out_vol(speaker, 100);
  if (esp_codec_dev_open(speaker, &format) != 0) {
    ESP_LOGE(kTag, "ES8311 open failed");
    vTaskDelete(nullptr);
    return;
  }

  short* synthesized = static_cast<short*>(heap_caps_calloc(
      2 * kClickSampleCount, sizeof(short),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (synthesized == nullptr) {
    ESP_LOGE(kTag, "fallback sound allocation failed");
    esp_codec_dev_close(speaker);
    vTaskDelete(nullptr);
    return;
  }
  short* clicky_down = synthesized;
  short* capacitive = synthesized + kClickSampleCount;
  synthesize_click(SoundProfile::Clicky, true, clicky_down, kClickSampleCount);
  synthesize_capacitive_swipe(capacitive, kClickSampleCount);

  Voice voices[kMaxVoices]{};
  short output[kMixFrames]{};
  AudioRequest request{};
  bool enabled = true;
  bool opened = true;
  unsigned char volume = 100;
  ESP_LOGI(kTag, "speaker ready, config-v2 switch samples + %u voices",
           kMaxVoices);

  while (true) {
    if (!any_voice(voices)) {
      if (xQueueReceive(queue, &request, portMAX_DELAY) != pdTRUE) continue;
      process_request(request, voices, clicky_down, capacitive, enabled, opened,
                      volume, format);
    }
    while (xQueueReceive(queue, &request, 0) == pdTRUE) {
      process_request(request, voices, clicky_down, capacitive, enabled, opened,
                      volume, format);
    }
    if (enabled && opened && any_voice(voices)) {
      mix_and_write(voices, volume, output);
    }
  }
}

bool enqueue_request(const AudioRequest& request, bool urgent = false) {
  if (queue == nullptr) return false;
  const BaseType_t result =
      urgent ? xQueueSendToFront(queue, &request, 0)
             : xQueueSend(queue, &request, 0);
  if (result == pdTRUE) return true;
  ++dropped_requests;
  if (dropped_requests == 1U || (dropped_requests % 16U) == 0U) {
    ESP_LOGW(kTag, "audio queue full, dropped=%u", dropped_requests);
  }
  return false;
}

}  // namespace

esp_err_t codex_audio_start() {
  if (queue != nullptr) return ESP_OK;
  queue = xQueueCreateWithCaps(32, sizeof(AudioRequest),
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (queue == nullptr) return ESP_ERR_NO_MEM;
  if (xTaskCreatePinnedToCore(audio_task, "codex_audio", 6144, nullptr, 9,
                             nullptr, 0) != pdPASS) {
    vQueueDelete(queue);
    queue = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void codex_audio_play(const SideEffect& effect, SoundProfile profile) {
  AudioRequest request{
      .type = AudioRequestType::Play,
      .profile = profile,
      .down = true,
      .capacitive = false,
      .variation = 0,
  };
  request_sequence.fetch_add(1, std::memory_order_relaxed);
  if (effect.type == SideEffectType::PlayKeyUpSound) {
    request.down = false;
  } else if (effect.type == SideEffectType::PlayCapacitiveSound) {
    request.capacitive = true;
  } else if (effect.type != SideEffectType::PlayKeyDownSound) {
    return;
  }
  enqueue_request(request);
}

void codex_audio_set_enabled(bool value) {
  const AudioRequest request{
      .type = value ? AudioRequestType::Enable : AudioRequestType::Disable,
  };
  if (!enqueue_request(request, true) && queue != nullptr) {
    xQueueReset(queue);
    xQueueSend(queue, &request, 0);
  }
}

void codex_audio_set_volume(unsigned char volume) {
  if (volume < 10 || volume > 100 || volume % 10 != 0) return;
  enqueue_request({
      .type = AudioRequestType::Volume,
      .volume = volume,
  }, true);
}

}  // namespace codex
