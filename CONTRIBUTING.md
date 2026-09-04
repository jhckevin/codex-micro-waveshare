# Contributing

请先阅读 [完整手册](docs/USER_GUIDE.zh-CN.md)，并确认改动只面向 Waveshare ESP32-S3-Touch-LCD-4B。

提交前至少运行：

```sh
cd firmware
bash tests/host/run-linux.sh
idf.py build
```

Windows 客户端还需运行：

```powershell
cd desktop
npm ci
npm test
npm run build
powershell -ExecutionPolicy Bypass -File native/build.ps1
```

Issue/PR 请写明板卡版本、固件提交、Windows/Codex 版本、USB 或 BLE、复现步骤和精简日志。不要提交密钥、设备身份、账户信息、构建目录或预编译 Release。

如果没有对应实机或 Windows 环境，请明确标注未验证。MIC 松开、协议回复和输入释放属于不可丢弃事件；性能优化不能通过忽略这些事件实现。
