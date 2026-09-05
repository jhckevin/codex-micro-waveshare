# codex-micro-waveshare

![设备实物运行效果](docs/images/product/device-front.jpg)

把微雪 **ESP32-S3-Touch-LCD-4B** 变成一个桌面 Codex 控制器：在触摸屏上选任务、操作命令、使用语音，也可以通过 Windows 客户端配置自己的按键层和键帽。

**[完整中文说明书：从接线、每个按键和设置，到自行编译与底座装配](docs/USER_GUIDE.zh-CN.md)**

这是针对上述特定板卡的非官方项目，与 OpenAI、Work Louder、Waveshare 无隶属或认证关系。[微雪官方硬件 Wiki](https://www.waveshare.net/wiki/ESP32-S3-Touch-LCD-4B)

## 三个部分

| 目录 | 里面有什么 |
|---|---|
| [firmware](firmware/) | 嵌入式固件：经典控制界面、USB / BLE、灯效、音效、六层按键与省电设置 |
| [desktop](desktop/) | 独立 Windows 客户端：按键映射、键帽、单色 SVG、音效和设备管理；也保留更新链路代码 |
| [hardware](hardware/) | 隐藏电池的倾斜底座，含 STL、Fusion 工程、螺丝与装配说明 |

固件连接路径已经针对 Windows 适配和验证；macOS 保留基础兼容设计，**未经实机验证**。Desktop **目前只针对 Windows 适配和验证**。

## 看看界面

以下设备图由真实固件 UI 离屏渲染，Desktop 图来自 QA 模拟设备。它们展示界面，不代表无线连接、实际更新或屏幕硬件验收。

| 设置中心 | Arcade 摇杆界面 |
|---|---|
| ![设备第一页设置](docs/images/embedded/settings-1.png) | ![Arcade界面](docs/images/embedded/arcade-joystick.png) |

![Windows客户端按键映射](docs/images/desktop/mic-typeless.png)

图标与动作可以分开配置：比如保留第一层的 Codex 操作，在第二层放复制、粘贴或自定义快捷键；也可以单独把 MIC 改成 Typeless 模板。高级路由依赖 Desktop 在线，失联时回退到经典操作。外环仍由 Codex 通道控制。

操作步骤、各设置的影响、三种 Arcade 控制方式，以及公开版更新功能的边界，都在 **[说明书](docs/USER_GUIDE.zh-CN.md)** 中。

## 3D 打印底座

底座提供约 21° 倾角，电池藏在下方。长方形电池**宽度不得超过 60 mm**；长度、厚度、保护板和引线仍需实测，并留出余量。

| 内部与固定柱 | 底面 |
|---|---|
| ![底座内部](docs/images/hardware/base-interior.jpg) | ![底座底面](docs/images/hardware/base-bottom.jpg) |

![装配后的倾斜效果](docs/images/hardware/device-on-base.jpg)

[v0.9 STL](hardware/print-files/Waveshare_86_21deg_v0.9_CODEX_FITCHECK.stl) · [Fusion 工程](hardware/models/Waveshare_86_21deg_v0.9_CODEX_DRAFT.f3d) · [零件清单与装配教程](docs/USER_GUIDE.zh-CN.md#parts)

## 使用前知道这几件事

- **可以不接电池，插 USB 使用。** 系统供电由板载电源管理路径处理；拔线即断电，没有电池缓冲。日常数据接原生 USB，刷写与日志接 USB TO UART。
- 当前充电策略针对 **606080、ATL A 类、3.7 V、4000 mAh / 14.8 Wh、带保护板**的特定电池组合。供应方能力参数是用户提供的信息，不是本项目独立认证的电池规格；更换电池需重新核对。
- 固件充电上限 **1.2 A**，按电池端 4.2 V 计算理论峰值约 **5 W**，不是持续实测功率。恒压收尾、供电条件和整机负载会影响速度。充电与续航估算见说明书，不承诺固定充满时间。
- 屏幕与动画耗电较高，提供智能屏保、多种待机和自动关机。**待机在暗室中仍可能看到微弱背光，尚未达到完全零发光的验收目标**；不能把软件截图全黑当作物理背光已断电，也未证明硬件不存在控制接口。
- 电池极性、截止电压、保护板、绝缘和固定必须正确。使用不匹配电池或改装可能造成短路、过热或起火；风险说明见手册，软件限流不能替代硬件安全检查。

## 自行编译

只提供源码，**不提供 Release、预编译固件、安装包或 portable 下载**。

固件使用 ESP-IDF 5.4.2；Windows Desktop 使用 Node.js 24，并需要 Windows C++ 构建工具。完整命令、依赖、刷写恢复和打包步骤见：

- [固件环境与编译](docs/USER_GUIDE.zh-CN.md#firmware-build)
- [Windows Desktop 编译与打包](docs/USER_GUIDE.zh-CN.md#desktop-build)

**当前公开版本已关闭加密套件**，默认不启用 Secure Boot、Flash Encryption 或 eFuse 写入。源码开关不能撤销开发板过去已烧写的 eFuse。

公开版不附带私有更新服务器、鉴权密钥或默认服务域名。更新界面和相关代码保留，但不等于存在开箱即用的公共升级服务。

## 许可

新增且独立拥有的原创内容采用 [PolyForm Noncommercial 1.0.0](LICENSES/PolyForm-Noncommercial-1.0.0.md)，**禁止商业用途**。第三方内容与已按 MIT 发布的历史内容继续遵循原许可，不追溯撤销既有授权。

因此这是源码公开项目，不属于 OSI 定义的开源软件。详细范围见 [LICENSE](LICENSE)、[许可证说明](LICENSES/README.zh-CN.md) 与 [第三方声明](firmware/NOTICE.md)。
