#pragma once

#include "Engine/Source/Base/Profiler.h"

#include <d3d9.h>

class GpuRenderTimer {
public:
    ~GpuRenderTimer() { Reset(); }

    GpuRenderTimer() = default;
    GpuRenderTimer(const GpuRenderTimer&) = delete;
    GpuRenderTimer& operator=(const GpuRenderTimer&) = delete;

    void Begin(IDirect3DDevice9* device) {
        if (!Profiler::Enabled() || (m_frame++ % SampleInterval) != 0 || m_open || m_pending == SlotCount) return;
        if (!m_frequencyQuery && !Initialize(device)) return;

        Slot& slot = m_slots[(m_read + m_pending) % SlotCount];
        if (slot.disjoint->Issue(D3DISSUE_BEGIN) != D3D_OK || slot.begin->Issue(D3DISSUE_END) != D3D_OK) {
            Stop();
            return;
        }
        // The previous CPU frame closes at the end of ScenePresent.
        slot.frameId = Profiler::CurrentFrameId() + 1;
        m_open = &slot;
    }

    void End() {
        if (!m_open) return;

        Slot& slot = *m_open;
        m_open = nullptr;
        if (slot.end->Issue(D3DISSUE_END) != D3D_OK || slot.disjoint->Issue(D3DISSUE_END) != D3D_OK) {
            Stop();
            return;
        }
        if (slot.frameId != Profiler::CurrentFrameId()) slot.frameId = 0;
        ++m_pending;
    }

    // Query flags are always zero. Leave pending results queued until a later frame.
    void Poll() {
        if (!m_pending) return;
        if (!m_frequency) {
            const HRESULT status = m_frequencyQuery->GetData(&m_frequency, sizeof(m_frequency), 0);
            if (status == S_FALSE) return;
            if (status != D3D_OK || !m_frequency) {
                Stop();
                return;
            }
        }

        while (m_pending) {
            const Slot& slot = m_slots[m_read];
            BOOL disjoint = FALSE;
            const HRESULT status = slot.disjoint->GetData(&disjoint, sizeof(disjoint), 0);
            if (status == S_FALSE) return;

            if (status == D3D_OK && !disjoint) {
                UINT64 start = 0;
                UINT64 end = 0;
                const HRESULT startStatus = slot.begin->GetData(&start, sizeof(start), 0);
                const HRESULT endStatus = slot.end->GetData(&end, sizeof(end), 0);
                if (startStatus == S_FALSE || endStatus == S_FALSE) return;

                if (startStatus == D3D_OK && endStatus == D3D_OK && end >= start) {
                    Profiler::RecordGpuRenderTime(slot.frameId, 1000.0 * static_cast<double>(end - start) / m_frequency);
                }
            }
            m_read = (m_read + 1) % SlotCount;
            --m_pending;
        }
    }

    void Reset() {
        for (Slot& slot : m_slots) {
            Release(slot.begin);
            Release(slot.end);
            Release(slot.disjoint);
        }
        Release(m_frequencyQuery);
        m_frequency = 0;
        m_open = nullptr;
        m_read = 0;
        m_pending = 0;
        m_frame = 0;
        m_attempted = false;
    }

private:
    static constexpr uint32_t SlotCount = 8;
    static constexpr uint32_t SampleInterval = 4;
    struct Slot {
        IDirect3DQuery9* begin = nullptr;
        IDirect3DQuery9* end = nullptr;
        IDirect3DQuery9* disjoint = nullptr;
        uint64_t frameId = 0;
    };

    static void Release(IDirect3DQuery9*& query) {
        if (query) {
            query->Release();
            query = nullptr;
        }
    }

    bool Initialize(IDirect3DDevice9* device) {
        if (m_attempted) return false;

        m_attempted = true;
        if (device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &m_frequencyQuery) != D3D_OK ||
            m_frequencyQuery->Issue(D3DISSUE_END) != D3D_OK) {
            Stop();
            return false;
        }
        for (Slot& slot : m_slots) {
            if (device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &slot.begin) != D3D_OK ||
                device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &slot.end) != D3D_OK ||
                device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &slot.disjoint) != D3D_OK) {
                Stop();
                return false;
            }
        }
        return true;
    }

    void Stop() {
        Reset();
        m_attempted = true;
    }

    Slot m_slots[SlotCount];
    Slot* m_open = nullptr;
    IDirect3DQuery9* m_frequencyQuery = nullptr;
    UINT64 m_frequency = 0;
    uint32_t m_frame = 0;
    uint32_t m_read = 0;
    uint32_t m_pending = 0;
    bool m_attempted = false;
};
