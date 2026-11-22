#pragma once

#include <cmath>

namespace wxl::atmosphere {

class FogWind
{
public:
    float Advance(double seconds, float speed, double period)
    {
        // Start at the former clock-based phase, then apply speed changes from the current position.
        m_offset = std::fmod(m_started ? m_offset + (seconds - m_seconds) * m_speed
                                       : seconds * speed, period);
        m_seconds = seconds;
        m_speed = speed;
        m_started = true;
        return static_cast<float>(m_offset);
    }

private:
    double m_offset = 0;
    double m_seconds = 0;
    float m_speed = 0;
    bool m_started = false;
};

}
