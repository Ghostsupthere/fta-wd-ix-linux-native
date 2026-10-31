#pragma once
// One opening transition per glider attachment; no movement ownership (wxl-goblin-glider,
// GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL).

#include <cstdint>

namespace wxl::glider
{
    enum class VisualAction { None, Open, Opened };

    struct OpeningSchedule
    {
        VisualAction Tick(uint32_t now, uint32_t duration)
        {
            if (!duration || duration > 5000)
                return VisualAction::None;
            if (!started)
            {
                started = true;
                startedMs = now;
                durationMs = duration;
                return VisualAction::Open;
            }
            if (!looping && now - startedMs >= durationMs)
            {
                looping = true;
                return VisualAction::Opened;
            }
            return VisualAction::None;
        }

        bool started = false;
        bool looping = false;
        uint32_t startedMs = 0;
        uint32_t durationMs = 0;
    };
}
