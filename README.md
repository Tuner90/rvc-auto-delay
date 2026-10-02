# AutoDelayMeasure / AutoDelayApply

> 一套用于 **RVC 实时变声延迟对齐** 的 VST3 插件（Windows x64 / VST3）

RVC 实时变声链会带来几百毫秒的处理延迟，而干声（原音）是实时的。两条声音一混，就会像「先出声、再回一声」，听起来发虚、不干净。这套插件用来把这段延迟量出来，再把环境音轨整体推后同样的时间，让两路重新对齐。

## 两个插件

| 插件 | 挂在哪 | 作用 |
|---|---|---|
| **AutoDelayMeasure** | RVC 输出轨 | 用侧链以「干声」为参考，测出 RVC 链整体晚多少毫秒；只测不补偿，界面上的 Send 会把读数推给所有 Apply |
| **AutoDelayApply** | 环境音轨 | 接收 Send 过来的值，把本轨音频整体推后，替代宿主的 Manual Track Delay |

两个插件是**独立的 VST3**（各自独立的 DLL，全局变量不共享），通过本机内存映射文件
`%APPDATA%\ChenFeng\delay_link.dat` 传递数值，使用无锁 seqlock 保护读写。

详细的**安装、接线、操作步骤、界面说明、参数表与常见问题**见 [使用说明.md](使用说明.md)（用户手册）。本 README 只讲「是什么」和「怎么编译」。

## 系统要求

- Windows x64
- 支持 VST3 的宿主（在 Studio One 7 上验证）
- 仅提供 VST3 格式

## 构建

需要准备：

- CMake ≥ 3.22
- Visual Studio 2022（MSVC，x64）
- JUCE 9 源码

先把 `CMakeLists.txt` 里的 `JUCE_DIR` 改成你自己的 JUCE 源码目录：

```cmake
set(JUCE_DIR "D:/UCWP/FormantShifter/libs/JUCE" CACHE PATH "JUCE 源码目录")
```

然后执行：

```powershell
cmake -S "<项目目录>" -B "<项目目录>\build" -G "Visual Studio 17 2022" -A x64
cmake --build "<项目目录>\build" --config Release
```

产物位置：

```
build\AutoDelayMeasure_artefacts\Release\VST3\AutoDelayMeasure.vst3
build\AutoDelayApply_artefacts\Release\VST3\AutoDelayApply.vst3
```

> **注意**：整个项目路径必须是纯 ASCII。JUCE 自带的 `juceaide` 在非 ASCII 路径下处理 `.rc` 资源文件时会直接崩溃，所以源码目录和构建目录都要放在纯英文路径下。
>
> 首次 configure 时 juceaide 自检可能失败一次（构建中的 exe 尚未释放），重跑同一条 configure 命令即可通过。

## 安装

把两个 `.vst3` 目录拷贝到 VST3 插件目录（`C:\Program Files\Common Files\VST3\`），
然后在宿主里重新扫描 VST3 插件即可。完整步骤见 [使用说明.md](使用说明.md) 的「二、安装」。

## 授权

本项目以 **GNU Affero General Public License v3.0（AGPL-3.0）** 发布，完整条款见 [LICENSE](LICENSE)。

本项目基于 **JUCE 9**（AGPLv3 / 商业双许可）开发。如需以闭源方式使用本项目代码，请自行获取 JUCE 商业授权。

第三方组件及其许可（JUCE、VST3 SDK 等）见 [THIRD_PARTY.md](THIRD_PARTY.md)。

### 免责声明

本项目按「现状」提供，不提供任何明示或暗示的担保，作者不对使用后果承担责任（详见 LICENSE 第 15、16 条）。

本项目为独立开发的第三方工具，与 **Antares**、**Steinberg**、**RVC** 官方均无任何关联。文档中出现的 `Auto-Tune`、`Auto-Key`、`VST3`、`Studio One` 等名称，均为其各自权利人的商标或注册商标，此处仅作说明性引用，不构成任何形式的赞助、隶属或背书关系。

Copyright (C) 2026 晨锋
