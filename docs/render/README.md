# 说明书图片的来源与复现

本目录只用于生成文档，不参与固件构建或运行。所有命令从仓库根目录执行。新图按用途保存在 images/embedded、images/desktop、images/diagrams；哈希和分类见 [manifest.json](manifest.json)。

## 三种图片不要混淆

- embedded：编译真实固件的 ui.cpp、布局、灯效和素材，以样例 DeviceState 在 LVGL 离屏显示器上绘制。不是另写一套 HTML 仿品，也不代表实际 LCD 的背光、刷新时序或无线状态。
- desktop：Playwright 操作 Desktop 的 QA 模拟设备，实际点击连接、按键配置、Typeless、第二层、设置与更新确认。73% 电量等数据是测试样例，不是实机测量。
- diagrams：说明性 HTML / SVG 功能示意截图；侧面图不表示真实机械尺寸或某批次接口排列，以实物丝印为准。

原有 product / hardware 实物照片和 classical-framebuffer.png 保留其原来源。文档没有把截图生成当成固件烧写、真实充电、快捷键注入或长期稳定性测试。

## LVGL 离屏截图

复现环境：Linux x86_64，GCC / G++ 13.3，CMake 3.30.2，Ninja 1.12.1，Python 3。LVGL 使用 firmware/dependencies.lock 对应版本。

先按说明书准备 ESP-IDF 5.4.2，在 firmware 执行 idf.py reconfigure，让组件管理器准备 managed_components/lvgl__lvgl；不要把 managed_components 提交到仓库。然后回仓库根目录：

```sh
cmake -G Ninja -S docs/render/lvgl -B /tmp/codex-docs-lvgl -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/codex-docs-lvgl -j4
/tmp/codex-docs-lvgl/capture
python3 docs/render/convert_ppm.py
```

capture.cpp 列出13个场景：经典、MIC按下、三页设置、无电池、告警、解锁、Arcade进入、三种控制与退出。模拟存储为空，内存接口映射到主机 malloc/free，绝不连接设备。生成 PPM 是中间产物，PNG 是文档使用的结果；确认转换完成后可删除本次生成的 PPM。

## Desktop 与示意图截图

准备 Node.js 24，先在 desktop 执行 npm ci。Playwright 安装在自己的工具目录，不需要改项目依赖：

```sh
mkdir -p /tmp/codex-docs-tools
npm install --prefix /tmp/codex-docs-tools playwright
/tmp/codex-docs-tools/node_modules/.bin/playwright install chromium
export PLAYWRIGHT_MODULE=/tmp/codex-docs-tools/node_modules/playwright
node docs/render/serve-desktop.mjs
```

该预览仅绑定127.0.0.1:14210，不对公网开放。在另一个终端，从仓库根目录执行：

```sh
export PLAYWRIGHT_MODULE=/tmp/codex-docs-tools/node_modules/playwright
node docs/render/capture-desktop.cjs
node docs/render/diagrams.cjs
```

可以通过 DOCS_DESKTOP_URL 指定其他本机 QA 预览地址。Linux容器运行需具备Chromium依赖；本次受控容器脚本带 --no-sandbox，不应把它照搬到正式Electron或公网浏览器服务。完成后关闭自己启动的预览进程。

capture-desktop.cjs 检查页面脚本异常，不验证真实设备。修改QA数据、字体或页面源码可能改变截图，需要重新检查内容和尺寸，不能只更新哈希。示意HTML由diagrams.cjs生成，维护JS中的源模板即可。

## 发布前检查

检查Markdown本地链接、图片引用、明确的模拟标注，以及手册中的文字是否与当前控件一致。出现控件裁切或旧提示文案时记录局限，不能用理想化图片遮盖实际问题。更新截图后同步更新manifest中的来源提交和SHA-256。

检查命令（从仓库根目录运行）：

```sh
python3 docs/render/validate.py
# 确认新截图正确后更新清单
python3 docs/render/validate.py --record
```
