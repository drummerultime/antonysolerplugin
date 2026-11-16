#pragma once

#include <cmath>
#include <algorithm>
#include "../Params.h"

namespace fragor
{
    inline float warpPhase (int mode, float p, float a, float fmIn) noexcept
    {
        switch ((Warp) mode)
        {
            case Warp::BendPlus:
            case Warp::BendMinus:
            {
                if (a < 1.0e-4f) return p;
                const float k = mode == (int) Warp::BendPlus ? 1.0f + a * 6.0f : 1.0f / (1.0f + a * 6.0f);
                return p < 0.5f ? 0.5f * std::pow (2.0f * p, k)
                                : 1.0f - 0.5f * std::pow (2.0f * (1.0f - p), k);
            }
            case Warp::Sync:
            {
                const float r = 1.0f + a * 7.0f;
                const float x = p * r;
                return x - std::floor (x);
            }
            case Warp::Pwm:
            {
                const float w = 0.5f - a * 0.49f;
                return p < w ? p * 0.5f / w : 0.5f + (p - w) * 0.5f / (1.0f - w);
            }
            case Warp::Mirror:
            {
                const float m = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
                const float y = p + a * (m - p);
                return y >= 1.0f ? 0.99999f : y;
            }
            case Warp::Quantize:
            {
                if (a < 1.0e-3f) return p;
                const float steps = 2.0f + (1.0f - a) * (1.0f - a) * 126.0f;
                return std::floor (p * steps) / steps;
            }
            case Warp::FmOther:
            case Warp::FmSub:
            {
                const float y = p + a * 1.6f * fmIn;
                return y - std::floor (y);
            }
            case Warp::Off: case Warp::Ring: case Warp::Count: default: return p;
        }
    }

    inline float warpFreqFactor (int mode, float a) noexcept
    {
        switch ((Warp) mode)
        {
            case Warp::Sync:     return 1.0f + a * 7.0f;
            case Warp::Pwm:      return 0.5f / std::max (0.01f, 0.5f - a * 0.49f);
            case Warp::Mirror:   return 1.0f + a;
            case Warp::BendPlus:
            case Warp::BendMinus:return 1.0f + a * 2.0f;
            case Warp::FmOther:
            case Warp::FmSub:    return 1.0f + a * 4.0f;
            case Warp::Ring:     return 1.0f + a;
            default:             return 1.0f;
        }
    }
}
