/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Common/DelayLink.cpp

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

#include "DelayLink.h"

#include <atomic>
#include <cstring>

namespace
{
    // 文件头部的自描述标记：宿主重启、系统重启后照样能认出这是我们的通道
    constexpr juce::uint32 kMagic   = 0x4B4C4441;  // 'A' 'D' 'L' 'K'（小端）
    constexpr juce::uint32 kVersion = 1;

    // 各字段在 32 字节里的偏移
    constexpr int kOffMagic   = 0;
    constexpr int kOffVersion = 4;
    constexpr int kOffSeq     = 8;
    constexpr int kOffValue   = 16;
    constexpr int kOffSentAt  = 24;

    // 这些读写都直接落在共享内存页上，必须逐字节 memcpy，
    // 不能假设对齐（映射起始地址虽然是页对齐的，但偏移 16/24 上的 double
    // 在 32 位下并不保证 8 字节对齐；memcpy 由编译器内联成单条指令）。
    juce::uint32 loadU32 (const volatile juce::uint8* p)
    {
        juce::uint32 v = 0;
        std::memcpy (&v, const_cast<const juce::uint8*> (p), sizeof (v));
        return v;
    }

    juce::uint64 loadU64 (const volatile juce::uint8* p)
    {
        juce::uint64 v = 0;
        std::memcpy (&v, const_cast<const juce::uint8*> (p), sizeof (v));
        return v;
    }

    double loadF64 (const volatile juce::uint8* p)
    {
        double v = 0.0;
        std::memcpy (&v, const_cast<const juce::uint8*> (p), sizeof (v));
        return v;
    }

    void storeU32 (volatile juce::uint8* p, juce::uint32 v) { std::memcpy (const_cast<juce::uint8*> (p), &v, sizeof (v)); }
    void storeU64 (volatile juce::uint8* p, juce::uint64 v) { std::memcpy (const_cast<juce::uint8*> (p), &v, sizeof (v)); }
    void storeF64 (volatile juce::uint8* p, double v)       { std::memcpy (const_cast<juce::uint8*> (p), &v, sizeof (v)); }

    // 确保共享文件存在且正好是 32 字节。
    //
    // 注意：juce::MemoryMappedFile 在 Windows 上会先取 file.getSize() 作为映射长度，
    // 文件不存在或长度为 0 时映射必然失败（CreateFileMapping 长度 0 = 整个文件 = 0）。
    // 所以必须先把文件建出来、把长度撑到 32，再去映射。
    void ensureFile (const juce::File& f)
    {
        if (f.existsAsFile() && f.getSize() == (juce::int64) DelayLink::kFileSize)
            return;

        juce::MemoryBlock mb ((size_t) DelayLink::kFileSize, true);  // 全 0
        storeU32 ((volatile juce::uint8*) mb.getData() + kOffMagic,   kMagic);
        storeU32 ((volatile juce::uint8*) mb.getData() + kOffVersion, kVersion);

        try
        {
            f.replaceWithData (mb.getData(), mb.getSize());
        }
        catch (...)
        {
            // 两个插件同时首启时可能撞车：另一边刚好建好了，那就直接用它的
        }
    }
}

//==============================================================================
juce::File DelayLink::getLinkFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("ChenFeng");

    if (! dir.isDirectory())
        dir.createDirectory();

    return dir.getChildFile ("delay_link.dat");
}

juce::String DelayLink::getLinkFilePath()
{
    return getLinkFile().getFullPathName();
}

//==============================================================================
DelayLink::DelayLink()
{
    const auto f = getLinkFile();
    ensureFile (f);

    mmf = std::make_unique<juce::MemoryMappedFile> (f, juce::MemoryMappedFile::readWrite);

    // 映射没成或长度不对，直接丢掉（对应 isOpen() == false）
    if (mmf != nullptr && (mmf->getData() == nullptr || mmf->getSize() < (size_t) kFileSize))
        mmf.reset();
}

DelayLink::~DelayLink() = default;

//==============================================================================
void DelayLink::write (double valueMs) noexcept
{
    if (mmf == nullptr)
        return;

    auto* p = static_cast<volatile juce::uint8*> (mmf->getData());

    if (p == nullptr)
        return;

    const juce::uint64 seq = loadU64 (p + kOffSeq);

    // ---- seqlock 写入三段式 ----
    // 1) 序号变奇数：告诉读者"数据正在被改，别用"
    std::atomic_thread_fence (std::memory_order_seq_cst);
    storeU64 (p + kOffSeq, seq + 1);
    std::atomic_thread_fence (std::memory_order_seq_cst);

    // 2) 写数据本体
    storeU32 (p + kOffMagic,   kMagic);
    storeU32 (p + kOffVersion, kVersion);
    storeF64 (p + kOffValue,   valueMs);
    storeF64 (p + kOffSentAt,  (double) juce::Time::currentTimeMillis());

    // 3) 序号变偶数：数据完整可用
    std::atomic_thread_fence (std::memory_order_seq_cst);
    storeU64 (p + kOffSeq, seq + 2);
    std::atomic_thread_fence (std::memory_order_seq_cst);
}

DelayLink::Snapshot DelayLink::read() noexcept
{
    Snapshot s;

    if (mmf == nullptr)
        return s;

    auto* p = static_cast<volatile juce::uint8*> (mmf->getData());

    if (p == nullptr)
        return s;

    // 最多试几次；正常情况一次就成（冲突窗口极短）
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const juce::uint64 s1 = loadU64 (p + kOffSeq);
        std::atomic_thread_fence (std::memory_order_seq_cst);

        if (s1 == 0)            // 从来没写过（或文件刚被重建）
            return s;

        if ((s1 & 1) != 0)      // 写入端正写到一半，重试
            continue;

        const juce::uint32 magic   = loadU32 (p + kOffMagic);
        const juce::uint32 version = loadU32 (p + kOffVersion);
        const double       value   = loadF64 (p + kOffValue);
        const double       sentAt  = loadF64 (p + kOffSentAt);
        std::atomic_thread_fence (std::memory_order_seq_cst);

        const juce::uint64 s2 = loadU64 (p + kOffSeq);

        if (s1 != s2)           // 读的过程中被改过，数据可能半新半旧，重试
            continue;

        if (magic != kMagic || version != kVersion)
            return s;           // 文件不是我们的（或版本对不上），当作没收到

        s.valid    = true;
        s.valueMs  = value;
        s.sentAtMs = sentAt;
        s.sequence = (juce::int64) s1;
        s.isNew    = ((juce::int64) s1 != lastSeq);
        lastSeq    = (juce::int64) s1;
        return s;
    }

    return s;
}
