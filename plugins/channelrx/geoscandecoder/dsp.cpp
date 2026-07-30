#include "dsp.hpp"

#include <cmath>

// Generate low-pass FIR filter coefficients — matches GNU Radio firdes::low_pass() with Blackman window
std::vector<float> generate_lowpass(
    float gain,
    float sample_rate,
    float cutoff,
    float transition
)
{
    double sr = sample_rate;
    double tr = transition;
    double fc = cutoff;

    int ntaps = (int)(sr / tr * 5.0);
    if (!(ntaps & 1))
        ntaps++;

    std::vector<float> taps(ntaps);

    // Blackman window coefficients
    int M = (ntaps - 1) / 2;
    double fwT0 = 2.0 * M_PI * fc / sr;

    for (int n = -M; n <= M; n++)
    {
        int idx = n + M;
        double w = 0.42 - 0.5 * cos(2.0 * M_PI * idx / (ntaps - 1))
                        + 0.08 * cos(4.0 * M_PI * idx / (ntaps - 1));

        double v;
        if (n == 0)
            v = fwT0 / M_PI;
        else
            v = sin(n * fwT0) / (n * M_PI);

        taps[idx] = (float)(v * w);
    }

    // Normalize: gain at DC should equal the requested gain
    double fmax = taps[0 + M];
    for (int n = 1; n <= M; n++)
        fmax += 2.0 * taps[n + M];

    double g = gain / fmax;
    for (auto &t : taps)
        t = (float)(t * g);

    return taps;
}

FIRFilter::FIRFilter(std::vector<float> t)
    : taps(std::move(t)),
      delay(taps.size(), 0)
{
}

float FIRFilter::processSample(float input)
{
    for (size_t i = delay.size() - 1; i > 0; i--)
        delay[i] = delay[i - 1];

    delay[0] = input;

    float acc = 0;

    for (size_t i = 0; i < taps.size(); i++)
        acc += delay[i] * taps[i];

    return acc;
}

int FractionalResampler::processSample(std::complex<float> sample, std::complex<float> *outputs, int maxOutputs)
{
    d_buffer[d_bufPos] = sample;
    d_bufPos = (d_bufPos + 1) % NTAPS;
    if (d_bufCount < NTAPS)
        d_bufCount++;

    if (d_bufCount < NTAPS)
        return 0;

    int count = 0;
    d_accum += 1.0;

    while (d_accum >= d_ratio && count < maxOutputs)
    {
        d_accum -= d_ratio;

        int imu = (int)(d_mu * NSTEPS);

        if (imu < 0)
            imu = 0;

        if (imu > NSTEPS)
            imu = NSTEPS;

        const float *t = dsp_detail::taps[imu];

        std::complex<float> out(0, 0);

        for (int i = 0; i < NTAPS; i++)
        {
            int idx = (d_bufPos - 1 - i + NTAPS) % NTAPS;
            out += d_buffer[idx] * t[i];
        }

        outputs[count++] = out;

        d_mu += d_ratio;
        d_mu -= floor(d_mu);
    }

    return count;
}

// Second-order clock tracking loop
// Based on GNU Radio clock_tracking_loop (GPL-3.0)
// https://github.com/gnuradio/gnuradio/blob/main/gr-digital/lib/clock_tracking_loop.h

ClockTrackingLoop::ClockTrackingLoop(float loop_bw, float max_period, float min_period,
                                     float nominal_period, float damping, float ted_gain)
    : d_avg_period(nominal_period),
      d_max_avg_period(max_period),
      d_min_avg_period(min_period),
      d_nom_avg_period(nominal_period),
      d_inst_period(nominal_period),
      d_phase(0.0f),
      d_zeta(damping),
      d_omega_n_norm(loop_bw),
      d_ted_gain(ted_gain),
      d_alpha(0.0f),
      d_beta(0.0f),
      d_prev_avg_period(nominal_period),
      d_prev_inst_period(nominal_period),
      d_prev_phase(0.0f)
{
    set_nom_avg_period(nominal_period);
    set_avg_period(d_nom_avg_period);
    set_inst_period(d_nom_avg_period);
    update_gains();
}

void ClockTrackingLoop::update_gains()
{
    float omega_n_T = d_omega_n_norm;
    float zeta_omega_n_T = d_zeta * omega_n_T;
    float k0 = 2.0f / d_ted_gain;
    float k1 = expf(-zeta_omega_n_T);
    float sinh_zeta_omega_n_T = sinhf(zeta_omega_n_T);

    float alpha, beta;

    if (d_zeta > 1.0f)
    {
        float omega_d_T = omega_n_T * sqrtf(d_zeta * d_zeta - 1.0f);
        float cosx_omega_d_T = coshf(omega_d_T);
        alpha = k0 * k1 * sinh_zeta_omega_n_T;
        beta  = k0 * (1.0f - k1 * (sinh_zeta_omega_n_T + cosx_omega_d_T));
    }
    else if (d_zeta == 1.0f)
    {
        alpha = k0 * k1 * sinh_zeta_omega_n_T;
        beta  = k0 * (1.0f - k1 * (sinh_zeta_omega_n_T + 1.0f));
    }
    else
    {
        float omega_d_T = omega_n_T * sqrtf(1.0f - d_zeta * d_zeta);
        float cosx_omega_d_T = cosf(omega_d_T);
        alpha = k0 * k1 * sinh_zeta_omega_n_T;
        beta  = k0 * (1.0f - k1 * (sinh_zeta_omega_n_T + cosx_omega_d_T));
    }

    d_alpha = alpha;
    d_beta = beta;
}

void ClockTrackingLoop::set_loop_bandwidth(float bw)
{
    d_omega_n_norm = bw;
    update_gains();
}

void ClockTrackingLoop::set_damping_factor(float df)
{
    d_zeta = df;
    update_gains();
}

void ClockTrackingLoop::set_ted_gain(float g)
{
    d_ted_gain = g;
    update_gains();
}

void ClockTrackingLoop::set_nom_avg_period(float period)
{
    if (period < d_min_avg_period || period > d_max_avg_period)
        d_nom_avg_period = (d_max_avg_period + d_min_avg_period) / 2.0f;
    else
        d_nom_avg_period = period;
}

// SymbolSync 

SymbolSync::SymbolSync(float sps, float loop_bw, float damping, float ted_gain, float max_dev)
    : d_clock(loop_bw,
              sps * (1.0f + max_dev),
              sps * (1.0f - max_dev),
              sps,
              damping,
              ted_gain),
      d_sps(sps),
      d_prev_on_time(0.0f),
      d_ted_initialized(false),
      d_mu(0.0f),
      d_idx(NTAPS - 1)
{
}

void SymbolSync::reset()
{
    d_clock.set_avg_period(d_sps);
    d_clock.set_inst_period(d_sps);
    d_clock.set_phase(0.0f);
    d_mu = 0.0f;
    d_prev_on_time = 0.0f;
    d_ted_initialized = false;
    d_buf.clear();
    d_idx = NTAPS - 1;
}

void SymbolSync::setSps(float sps)
{
    d_sps = sps;
    float max_dev = (d_clock.get_max_avg_period() - d_clock.get_min_avg_period()) / (2.0f * sps);
    d_clock.set_max_avg_period(sps * (1.0f + max_dev));
    d_clock.set_min_avg_period(sps * (1.0f - max_dev));
    d_clock.set_nom_avg_period(sps);
    d_clock.set_avg_period(sps);
    d_clock.set_inst_period(sps);
}

void SymbolSync::setMaxDev(float max_dev)
{
    d_clock.set_max_avg_period(d_sps * (1.0f + max_dev));
    d_clock.set_min_avg_period(d_sps * (1.0f - max_dev));
}

void SymbolSync::setLoopParams(float loop_bw, float damping, float ted_gain)
{
    d_clock.set_loop_bandwidth(loop_bw);
    d_clock.set_damping_factor(damping);
    d_clock.set_ted_gain(ted_gain);
}

bool SymbolSync::processSample(float sample, float &output)
{
    d_buf.push_back(sample);

    if (d_buf.size() <= d_idx + NTAPS)
        return false;

    float mu = d_mu;
    float inst_period = d_clock.get_inst_period();

    float on_time = dsp_detail::mmse_interpolate<float>(&d_buf[d_idx - (NTAPS - 1)], mu);

    float mid_mu = mu - inst_period * 0.5f;
    size_t mid_idx = d_idx;

    while (mid_mu < 0.0f && mid_idx > NTAPS - 1)
    {
        mid_mu += 1.0f;
        --mid_idx;
    }

    float midpoint = 0.0f;

    if (mid_mu >= 0.0f && mid_idx >= NTAPS - 1 && mid_idx < d_buf.size())
        midpoint = dsp_detail::mmse_interpolate<float>(&d_buf[mid_idx - (NTAPS - 1)], mid_mu);

    if (!d_ted_initialized)
    {
        d_prev_on_time = on_time;
        d_ted_initialized = true;
    }

    float error = (d_prev_on_time - on_time) * midpoint;
    d_prev_on_time = on_time;

    d_clock.advance_loop(error);
    d_clock.phase_wrap();

    float new_mu = mu + d_clock.get_inst_period();

    int n_advance = (int)std::floor(new_mu);
    if (n_advance < 1)
        n_advance = 1;
    d_mu = new_mu - std::floor(new_mu);
    if (d_mu >= 1.0f)
        d_mu -= std::floor(d_mu);
    d_idx += (size_t)n_advance;

    output = on_time;

    if (d_idx > 8192)
    {
        size_t keep = NTAPS - 1;
        size_t toErase = d_idx - keep;
        if (toErase > d_buf.size())
            toErase = d_buf.size() > keep ? d_buf.size() - keep : 0;
        d_buf.erase(d_buf.begin(), d_buf.begin() + toErase);
        d_idx = d_buf.size() > keep ? keep : d_buf.size();
    }

    return true;
}
