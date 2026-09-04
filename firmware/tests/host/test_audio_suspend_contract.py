from pathlib import Path


audio = Path("components/codex_audio/audio.cpp").read_text(encoding="utf-8")
start = audio.index("if (request.type == AudioRequestType::Enable)")
end = audio.index("if (!enabled || !opened) return;", start)
standby_path = audio[start:end]

assert "bsp_audio_stream_suspend()" in standby_path
assert "bsp_audio_stream_resume()" in standby_path
assert "esp_codec_dev_close(speaker)" not in standby_path
assert "esp_codec_dev_open(speaker" not in standby_path
