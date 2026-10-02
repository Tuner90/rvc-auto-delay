# ============================================================================
# AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
#
# Copyright (C) 2026 晨锋
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# ============================================================================

cmake_minimum_required(VERSION 3.22)

project(AutoDelayMeasure VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# 依赖：直接复用本地已有的 JUCE 源码，不再下一份（省约 1GB）
# 如需换成独立副本，把 JUCE_DIR 指过去即可。
# ============================================================================
set(JUCE_DIR "D:/UCWP/FormantShifter/libs/JUCE" CACHE PATH "JUCE 源码目录")
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce_build")

# 不做自动部署：不设 COPY_PLUGIN_AFTER_BUILD，也不设 JUCE_VST3_COPY_DIR。
# 编译产物落在 build\<目标名>_artefacts\Release\VST3\，
# 由脚本手动拷贝到 VST3\ 目录。
#
# 【重要】整个项目路径必须是纯 ASCII。
#   JUCE 自带的 juceaide 工具在非 ASCII 路径下处理 .rc 资源文件时会直接崩溃
#   （Unhandled exception），所以源码目录、构建目录都放在纯英文路径下。
#   （本目录原先叫「自动延迟测算」，已改名为 AutoDelayMeasure。）
#
#   cmake -S "D:\UCWP\AutoDelayMeasure" -B "D:\UCWP\AutoDelayMeasure\build" -G "Visual Studio 17 2022" -A x64
#   cmake --build "D:\UCWP\AutoDelayMeasure\build" --config Release
#
#   首次 configure 时 juceaide 自检可能失败一次（构建中的 exe 尚未释放），
#   直接重跑同一条 configure 命令即可通过。

# ============================================================================
# 两个插件共用的东西
#
#   AutoDelayMeasure  挂 RVC 轨，测延迟，有 Send 按钮
#   AutoDelayApply    挂环境轨，接收 Send 过来的值，把本轨音频延后
#
#   两者是不同的 .vst3（各自独立 DLL，全局变量不共享），
#   靠 Source/Common/DelayLink.* 里的「内存映射文件」通信。
# ============================================================================
set(AD_COMMON_SOURCES  Source/Common/DelayLink.cpp)
set(AD_COMMON_INCLUDES Source/Common)

set(AD_SHARED_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# ============================================================================
# 目标 1：AutoDelayMeasure（测量端）
# ============================================================================
juce_add_plugin(AutoDelayMeasure
    COMPANY_NAME              "ChenFeng"
    PLUGIN_MANUFACTURER_CODE  Chen
    PLUGIN_CODE               Adms
    PRODUCT_NAME              "AutoDelayMeasure"
    PLUGIN_NAME               "AutoDelayMeasure"
    DESCRIPTION               "Auto delay measurement with sidechain"
    FORMATS                   VST3
    VST3_CATEGORIES           "Fx"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    # 关掉自动生成的 moduleinfo.json。
    # JUCE 的 vst3_helper 在中文 / 日文等非英文系统上会把里面 Class 的 Name
    # 写成一串 \uFFxx 乱码（UTF-8 字节被逐字节加 0xFF00），宿主读到就是乱码名。
    # 这个文件在 VST3 规范里是可选的：没有它，宿主会直接加载插件本体读名字。
    # 插件名现已统一为纯 ASCII，从根上避开编码问题。
    VST3_AUTO_MANIFEST        FALSE
)

target_sources(AutoDelayMeasure PRIVATE
    Source/Measure/DelayAnalyzer.cpp
    Source/Measure/PluginProcessor.cpp
    Source/Measure/PluginEditor.cpp
    ${AD_COMMON_SOURCES}
)

target_include_directories(AutoDelayMeasure PRIVATE ${AD_COMMON_INCLUDES})

target_compile_definitions(AutoDelayMeasure PUBLIC ${AD_SHARED_DEFS})

target_link_libraries(AutoDelayMeasure
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ============================================================================
# 目标 2：AutoDelayApply（接收端）
# ============================================================================
juce_add_plugin(AutoDelayApply
    COMPANY_NAME              "ChenFeng"
    PLUGIN_MANUFACTURER_CODE  Chen
    PLUGIN_CODE               Adma
    PRODUCT_NAME              "AutoDelayApply"
    PLUGIN_NAME               "AutoDelayApply"
    DESCRIPTION               "Apply the delay sent by AutoDelayMeasure"
    FORMATS                   VST3
    VST3_CATEGORIES           "Fx"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    VST3_AUTO_MANIFEST        FALSE
)

target_sources(AutoDelayApply PRIVATE
    Source/Apply/PluginProcessor.cpp
    Source/Apply/PluginEditor.cpp
    ${AD_COMMON_SOURCES}
)

target_include_directories(AutoDelayApply PRIVATE ${AD_COMMON_INCLUDES})

target_compile_definitions(AutoDelayApply PUBLIC ${AD_SHARED_DEFS})

target_link_libraries(AutoDelayApply
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# MSVC 相关：
#   /utf-8 —— 源码为 UTF-8（含中文注释），不加会被按 GBK 解码而破坏语法
#   /O2    —— Release 优化
if (MSVC)
    foreach(AD_TARGET AutoDelayMeasure AutoDelayApply)
        target_compile_options(${AD_TARGET} PRIVATE
            /utf-8
            $<$<CONFIG:Release>:/O2>
        )
    endforeach()
endif()
