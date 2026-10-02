# 第三方组件与许可

本项目在构建与运行时使用了以下第三方组件。它们的版权归各自作者所有，
本项目按各自的许可条款使用，并在此保留其许可声明。

---

## JUCE 9

- **用途**：音频插件框架（插件封装、DSP、GUI、参数系统）
- **许可**：**AGPLv3** 或 JUCE 商业许可（双许可）
- **本项目采用**：AGPLv3
- **版权**：© Raw Material Software Limited
- **官网**：https://juce.com
- **许可全文**：见 JUCE 源码仓库的 `LICENSE.md`，或 https://www.gnu.org/licenses/agpl-3.0.html

> 说明：由于本项目以 AGPLv3 发布，任何基于本项目（含 JUCE）再分发、或以网络服务形式
> 提供的衍生作品，都必须同样以 AGPLv3 开放源代码。如需闭源使用，请自行获取 JUCE 商业授权。

---

## VST3 SDK（随 JUCE 内置）

- **用途**：VST3 插件格式支持（构建时由 JUCE 编译）
- **许可**：**MIT License**
- **版权**：© 2025 Steinberg Media Technologies GmbH
- **位置**：JUCE 源码 `modules/juce_audio_processors_headless/format_types/VST3_SDK/`

MIT 许可允许自由使用、修改、再分发，只需保留原始版权与许可声明——本文件即用于履行该义务。

---

## 关于「无关联」

本项目为独立开发的第三方工具，与 **Antares**、**Steinberg**、**RVC** 官方均无隶属或合作关系。

文档中提及的产品名称（如 `Auto-Tune`、`Auto-Key`、`VST3`、`Studio One`）均为其各自权利人的
商标或注册商标，此处仅用于说明兼容性或作对比，不构成任何形式的赞助、隶属或背书。

---

*以上信息基于本项目发布时的实际情况整理；如有变更，以各组件官方许可条款为准。*
