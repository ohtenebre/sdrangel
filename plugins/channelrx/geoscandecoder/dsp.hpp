#pragma once

#include "mmse_fir_interpolator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// Generate low-pass FIR filter coefficients
std::vector<float> generate_lowpass(
    float gain,
    float sample_rate,
    float cutoff,
    float transition
);

// Low Pass Filter with blackman window
class FIRFilter {
  private:
    std::vector<float> taps;
    std::vector<float> delay;
  public:
    FIRFilter(std::vector<float> t);

    float processSample(float input);

    std::vector<float> process(const std::vector<float> &input);

    void reset()
    {
        std::fill(delay.begin(), delay.end(), 0.0f);
    }
};

class FrequencyShifter {
  private:
    float m_phase = 0.0f;
    float m_phaseStep = 0.0f;
  public:
    void setFrequency(float freqOffset, float sampleRate)
    {
        m_phaseStep = 2.0f * M_PIf * freqOffset / sampleRate;
    }

    std::complex<float> processSample(std::complex<float> s)
    {
        float si, co;
        sincosf(m_phase, &si, &co);

        s *= std::complex<float>(co, si);

        m_phase += m_phaseStep;

        if (m_phase > M_PIf)
            m_phase -= 2.0f * M_PIf;
        else if (m_phase < -M_PIf)
            m_phase += 2.0f * M_PIf;

        return s;
    }

    void reset()
    {
        m_phase = 0.0f;
    }
};

// Fractional resampling using MMSE interpolation (streaming)
class FractionalResampler {
  private:
    double d_mu;
    double d_ratio;
    double d_accum;

    static constexpr int NTAPS = 8;
    static constexpr int NSTEPS = 128;

    std::complex<float> d_buffer[NTAPS] = {};
    int d_bufPos = 0;
    int d_bufCount = 0;

  public:
    FractionalResampler(double ratio) : d_mu(0.0), d_ratio(ratio), d_accum(0.0) {}

    void reset()
    {
        d_mu = 0.0;
        d_accum = 0.0;
        d_bufPos = 0;
        d_bufCount = 0;
        std::fill(std::begin(d_buffer), std::end(d_buffer), std::complex<float>(0, 0));
    }

    void setRatio(double ratio) { d_ratio = ratio; }
    double getRatio() const { return d_ratio; }

    int processSample(std::complex<float> sample, std::complex<float> *outputs, int maxOutputs);
};

// Second-order clock tracking loop — direct port of GNU Radio clock_tracking_loop
class ClockTrackingLoop {
  private:
    float d_avg_period;
    float d_max_avg_period, d_min_avg_period, d_nom_avg_period;
    float d_inst_period;
    float d_phase;
    float d_zeta;
    float d_omega_n_norm;
    float d_ted_gain;
    float d_alpha; // proportional gain (gain_mu)
    float d_beta;  // integral gain (gain_omega)
    float d_prev_avg_period, d_prev_inst_period, d_prev_phase;

  public:
    ClockTrackingLoop(float loop_bw, float max_period, float min_period,
                      float nominal_period, float damping, float ted_gain);

    void update_gains();

    void advance_loop(float error)
    {
        d_prev_avg_period = d_avg_period;
        d_prev_inst_period = d_inst_period;
        d_prev_phase = d_phase;

        d_avg_period += d_beta * error;
        period_limit();

        d_inst_period = d_avg_period + d_alpha * error;
        if (d_inst_period <= 0.0f)
            d_inst_period = d_avg_period;

        d_phase += d_inst_period;
    }

    void revert_loop()
    {
        d_avg_period = d_prev_avg_period;
        d_inst_period = d_prev_inst_period;
        d_phase = d_prev_phase;
    }

    void phase_wrap()
    {
        float limit = d_avg_period / 2.0f;
        while (d_phase > limit)
            d_phase -= d_avg_period;
        while (d_phase <= -limit)
            d_phase += d_avg_period;
    }

    void period_limit()
    {
        if (d_avg_period > d_max_avg_period)
            d_avg_period = d_max_avg_period;
        else if (d_avg_period < d_min_avg_period)
            d_avg_period = d_min_avg_period;
    }

    void set_loop_bandwidth(float bw);
    void set_damping_factor(float df);
    void set_ted_gain(float g);
    void set_avg_period(float p)  { d_avg_period = p; d_prev_avg_period = p; }
    void set_inst_period(float p) { d_inst_period = p; d_prev_inst_period = p; }
    void set_phase(float p)       { d_phase = p; d_prev_phase = p; }
    void set_max_avg_period(float p) { d_max_avg_period = p; }
    void set_min_avg_period(float p) { d_min_avg_period = p; }
    void set_nom_avg_period(float p);

    float get_loop_bandwidth() const  { return d_omega_n_norm; }
    float get_damping_factor() const  { return d_zeta; }
    float get_ted_gain() const        { return d_ted_gain; }
    float get_alpha() const           { return d_alpha; }
    float get_beta() const            { return d_beta; }
    float get_avg_period() const      { return d_avg_period; }
    float get_inst_period() const     { return d_inst_period; }
    float get_phase() const           { return d_phase; }
    float get_max_avg_period() const  { return d_max_avg_period; }
    float get_min_avg_period() const  { return d_min_avg_period; }
    float get_nom_avg_period() const  { return d_nom_avg_period; }
};

// Symbol timing recovery using Gardner TED (streaming)
class SymbolSync {
  private:
    ClockTrackingLoop d_clock;
    float d_sps;
    float d_prev_on_time;
    bool d_ted_initialized;

    float d_mu;

    static constexpr int NTAPS = 8;

    std::vector<float> d_buf;
    size_t d_idx;

  public:
    SymbolSync(float sps, float loop_bw, float damping, float ted_gain, float max_dev);

    void reset();

    void setSps(float sps);
    float getSps() const { return d_sps; }
    void setLoopParams(float loop_bw, float damping, float ted_gain);
    void setMaxDev(float max_dev);

    bool processSample(float sample, float &output);
};
