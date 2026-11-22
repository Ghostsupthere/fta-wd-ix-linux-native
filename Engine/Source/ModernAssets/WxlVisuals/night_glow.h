#pragma once

#include <algorithm>

namespace wxl::atmosphere {

class NightGlowHandoff
{
public:
    float Advance(double seconds, bool stockGlow)
    {
        const float target = stockGlow ? 0.0f : 1.0f;
        if (m_started)
        {
            const auto elapsed = static_cast<float>(std::clamp(seconds - m_seconds, 0.0, 0.1));
            m_weight += std::clamp(target - m_weight, -elapsed / 0.16f, elapsed / 0.25f);
        }
        else
            m_weight = target;
        m_seconds = seconds;
        m_started = true;
        return m_weight;
    }

    void Reset() { m_started = false; }

private:
    double m_seconds = 0;
    float m_weight = 0;
    bool m_started = false;
};

}
