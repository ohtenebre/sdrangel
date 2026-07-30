#pragma once
#include "mmse_interp_taps.hpp"

#include <algorithm>
#include <stdexcept>

/*
 * Uses coefficient table from GNU Radio.
 * See third_party/gnuradio/mmse_interp_taps.hpp
 */

namespace dsp_detail
{
    template <typename sample_t>
    inline sample_t mmse_interpolate(const sample_t *input, float mu)
    {
        mu = std::clamp(mu, 0.0f, 1.0f);

        int imu = static_cast<int>(mu * NSTEPS);

        const float *t = taps[imu];

        sample_t acc = 0;

        for (int k = 0; k < NTAPS; k++)
        {
            acc += t[k] * input[NTAPS - 1 - k];
        }

        return acc;
    }

} // namespace dsp_detail
