/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Common/DelayLink.h

    Copyright (C) 2026 晨锋

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_core/juce_core.h>

#include <memory>

/**
    DelayLink —— AutoDelayMeasure 与 AutoDelayApply 之间的跨插件通信通道。

    为什么不能直接用全局变量：
      两个插件是两个独立的 .vst3（独立 DLL），各自有独立的全局数据段，
      在一个里写的变量，另一个根本看不见。宿主也不会替我们传递。
      （Antares 的 Auto-Key / Auto-Tune Pro 也是两个独立插件文件，同样必须走系统级通道。）

    所以这里用【内存映射文件】：
      - 路径：%APPDATA%\ChenFeng\delay_link.dat（固定 32 字节）
      - 两个插件各自把同一个文件映射进自己的地址空间，
        看到的是同一份物理内存页 —— 这就是有效通信。

    并发保护（seqlock，无锁）：
      写入端：序号 +1（变奇数，表示"正在写"）→ 写数据 → 序号 +1（变偶数，表示"写完了"）
      读取端：读序号 → 读数据 → 再读序号；两次序号不一致或为奇数，就重读。
      好处：读取端永远不会阻塞，也不会读到"半新半旧"的撕裂数据。

    线程约定：
      - write() 只在消息线程（按钮点击）调用，不在音频线程。
      - read() 只在定时器回调（默认 200ms）调用，不在音频线程。
      映射本身在构造时建立一次，音频线程完全不碰这个类。
*/
class DelayLink
{
public:
    /** 要通信的数据量：4(magic) + 4(版本) + 8(序号) + 8(毫秒值) + 8(发送时刻) = 32 字节 */
    static constexpr int kFileSize = 32;

    /** 一次读取的快照。 */
    struct Snapshot
    {
        bool         valid      = false;  // 读到过至少一次有效写入
        bool         isNew      = false;  // 相对上一次 read() 是新值（值变过）
        double       valueMs    = 0.0;    // 发送端给的延迟值（毫秒）
        double       sentAtMs   = 0.0;    // 发送时刻（Unix 纪元毫秒，跨进程可比）
        juce::int64  sequence   = 0;      // 写入序号，偶数=完整，奇数=正在写
    };

    DelayLink();
    ~DelayLink();

    /** 映射是否建立成功。失败（权限/磁盘等）时 write/read 都是空操作。 */
    bool isOpen() const noexcept { return mmf != nullptr; }

    /** 发送一个值（写入端调用）。 */
    void write (double valueMs) noexcept;

    /** 读一次（接收端调用）。内部记住上次序号，用 isNew 告诉调用方值是否变过。 */
    Snapshot read() noexcept;

    /** 共享文件位置，便于排查问题。 */
    static juce::File   getLinkFile();
    static juce::String getLinkFilePath();

private:
    std::unique_ptr<juce::MemoryMappedFile> mmf;
    juce::int64 lastSeq = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayLink)
};
