#include <cmath>
#include "tle_utils.hpp"

#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <iomanip>

std::string tle_trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string tle_extract_norad_id(const std::string& tle1) {
    std::string stripped = tle_trim(tle1);
    if (stripped.length() >= 8) {
        size_t i = 1;
        while (i < stripped.length() && stripped[i] == ' ') i++;
        size_t j = i;
        while (j < stripped.length() && std::isdigit(stripped[j])) j++;
        if (j > i) return stripped.substr(i, j - i);
    }
    return "UNKNOWN";
}

static bool tle_parse_epoch(const std::string& tle1, int& year, double& day_of_year) {
    std::string s = tle_trim(tle1);
    if (s.length() < 32) return false;
    try {
        int epoch_yy = std::stoi(tle_trim(s.substr(18, 2)));
        day_of_year = std::stod(tle_trim(s.substr(20, 12)));
        year = (epoch_yy < 57) ? (2000 + epoch_yy) : (1900 + epoch_yy);
        return true;
    } catch (...) {
        return false;
    }
}

std::pair<bool, std::string> tle_check_freshness(const std::string& tle1) {
    int year;
    double doy;
    if (!tle_parse_epoch(tle1, year, doy)) {
        return {false, "Failed to determine TLE epoch date"};
    }

    std::tm epoch_tm = {};
    epoch_tm.tm_year = year - 1900;
    epoch_tm.tm_mon = 0;
    epoch_tm.tm_mday = 1;
    time_t epoch_tt = timegm(&epoch_tm);
    epoch_tt += static_cast<time_t>((doy - 1.0) * 86400.0);

    time_t now = time(nullptr);
    double age_days = difftime(now, epoch_tt) / 86400.0;

    if (age_days < 0) {
        return {false, "TLE from the future"};
    }
    if (age_days > TLE_MAX_AGE_DAYS) {
        std::ostringstream oss;
        oss << "TLE stale: " << std::fixed << std::setprecision(1) << age_days << " days";
        return {false, oss.str()};
    }

    std::ostringstream oss;
    oss << "TLE fresh: " << std::fixed << std::setprecision(1) << age_days << " days";
    return {true, oss.str()};
}

static bool is_tle_line1(const std::string& line) {
    return line.length() >= 10 && line[0] == '1';
}

static bool is_tle_line2(const std::string& line) {
    return line.length() >= 10 && line[0] == '2';
}

std::vector<TleData> tle_load_file(const std::string& filename) {
    std::vector<TleData> result;
    std::ifstream file(filename);
    if (!file.is_open()) return result;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        line = tle_trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    file.close();

    if (lines.size() < 2) return result;

    size_t i = 0;
    while (i < lines.size()) {
        TleData sat;

        if (is_tle_line1(lines[i]) && i + 1 < lines.size() && is_tle_line2(lines[i + 1])) {
            sat.line1 = lines[i];
            sat.line2 = lines[i + 1];
            sat.norad_id = tle_extract_norad_id(sat.line1);

            if (i >= 1) {
                sat.name = tle_trim(lines[i - 1]);
            } else {
                sat.name = "NORAD " + sat.norad_id;
            }

            sat.valid = true;
            result.push_back(sat);
            i += 2;
        } else if (i + 2 < lines.size() &&
                   is_tle_line1(lines[i + 1]) && is_tle_line2(lines[i + 2])) {
            sat.name = lines[i];
            sat.line1 = lines[i + 1];
            sat.line2 = lines[i + 2];
            sat.norad_id = tle_extract_norad_id(sat.line1);
            sat.valid = true;
            result.push_back(sat);
            i += 3;
        } else {
            i++;
        }
    }

    return result;
}

TleData tle_load_single(const std::string& line1, const std::string& line2, const std::string& name) {
    TleData sat;
    sat.line1 = tle_trim(line1);
    sat.line2 = tle_trim(line2);
    sat.name = name;
    sat.norad_id = tle_extract_norad_id(sat.line1);
    sat.valid = is_tle_line1(sat.line1) && is_tle_line2(sat.line2);
    return sat;
}

double tle_calc_gmst(double jd) {
    double d = jd - 2451545.0;
    double gmst_deg = 280.46061837 + 360.98564736629 * d;
    gmst_deg = std::fmod(gmst_deg, 360.0);
    if (gmst_deg < 0) gmst_deg += 360.0;
    return gmst_deg * TLE_DEG2RAD;
}

void tle_eci_to_ecef(double x, double y, double z, double gmst,
                     double& x_out, double& y_out, double& z_out) {
    double c = std::cos(gmst);
    double s = std::sin(gmst);
    x_out = x * c + y * s;
    y_out = -x * s + y * c;
    z_out = z;
}

void tle_ecef_to_lat_lon_alt(double x, double y, double z,
                             double& lat, double& lon, double& alt) {
    lon = std::atan2(y, x) * TLE_RAD2DEG;
    double p = std::sqrt(x * x + y * y);
    lat = std::atan2(z, p * (1.0 - TLE_WGS84_E2)) * TLE_RAD2DEG;

    for (int i = 0; i < 8; ++i) {
        double lat_rad = lat * TLE_DEG2RAD;
        double N = TLE_WGS84_A / std::sqrt(1.0 - TLE_WGS84_E2 * std::sin(lat_rad) * std::sin(lat_rad));
        double h = p / std::cos(lat_rad) - N;
        lat = std::atan2(z, p * (1.0 - TLE_WGS84_E2 * N / (N + h))) * TLE_RAD2DEG;
    }

    double lat_rad = lat * TLE_DEG2RAD;
    double N = TLE_WGS84_A / std::sqrt(1.0 - TLE_WGS84_E2 * std::sin(lat_rad) * std::sin(lat_rad));
    alt = p / std::cos(lat_rad) - N;
}

void tle_observer_ecef(double lat_deg, double lon_deg, double alt_km,
                       double& x, double& y, double& z) {
    double lat_rad = lat_deg * TLE_DEG2RAD;
    double lon_rad = lon_deg * TLE_DEG2RAD;
    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);
    double sin_lon = std::sin(lon_rad);
    double cos_lon = std::cos(lon_rad);

    double N = TLE_WGS84_A / std::sqrt(1.0 - TLE_WGS84_E2 * sin_lat * sin_lat);
    x = (N + alt_km) * cos_lat * cos_lon;
    y = (N + alt_km) * cos_lat * sin_lon;
    z = (N * (1.0 - TLE_WGS84_E2) + alt_km) * sin_lat;
}

void tle_observer_eci(double lat_deg, double lon_deg, double alt_km, double gmst,
                      double& x, double& y, double& z) {
    double x_ecef, y_ecef, z_ecef;
    tle_observer_ecef(lat_deg, lon_deg, alt_km, x_ecef, y_ecef, z_ecef);
    double c = std::cos(gmst);
    double s = std::sin(gmst);
    x = x_ecef * c - y_ecef * s;
    y = x_ecef * s + y_ecef * c;
    z = z_ecef;
}

void tle_observer_velocity_eci(double lat_deg, double lon_deg, double alt_km, double gmst,
                               double& vx, double& vy, double& vz) {
    double x, y, z;
    tle_observer_eci(lat_deg, lon_deg, alt_km, gmst, x, y, z);
    vx = -y * TLE_OMEGA_EARTH;
    vy = x * TLE_OMEGA_EARTH;
    vz = 0.0;
}

bool tle_check_earth_occlusion(const double sat_ecef[3], const double obs_ecef[3]) {
    double dx = sat_ecef[0] - obs_ecef[0];
    double dy = sat_ecef[1] - obs_ecef[1];
    double dz = sat_ecef[2] - obs_ecef[2];
    double seg_len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (seg_len < 1e-6) return false;

    double ux = dx / seg_len;
    double uy = dy / seg_len;
    double uz = dz / seg_len;
    double dot = obs_ecef[0] * ux + obs_ecef[1] * uy + obs_ecef[2] * uz;
    double closest_dist;

    if (dot < 0) {
        closest_dist = std::sqrt(obs_ecef[0] * obs_ecef[0] + obs_ecef[1] * obs_ecef[1] + obs_ecef[2] * obs_ecef[2]);
    } else if (dot > seg_len) {
        closest_dist = std::sqrt(sat_ecef[0] * sat_ecef[0] + sat_ecef[1] * sat_ecef[1] + sat_ecef[2] * sat_ecef[2]);
    } else {
        double cx = obs_ecef[0] + dot * ux;
        double cy = obs_ecef[1] + dot * uy;
        double cz = obs_ecef[2] + dot * uz;
        closest_dist = std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return closest_dist < 6371.0;
}

void tle_calc_azimuth_elevation(double obs_lat, double obs_lon, double /*obs_alt_km*/,
                                const double sat_ecef[3], const double obs_ecef[3],
                                double& azimuth, double& elevation) {
    double rx = sat_ecef[0] - obs_ecef[0];
    double ry = sat_ecef[1] - obs_ecef[1];
    double rz = sat_ecef[2] - obs_ecef[2];
    double dist = std::sqrt(rx * rx + ry * ry + rz * rz);

    double obs_r = std::sqrt(obs_ecef[0] * obs_ecef[0] + obs_ecef[1] * obs_ecef[1] + obs_ecef[2] * obs_ecef[2]);
    double up_x = obs_ecef[0] / obs_r;
    double up_y = obs_ecef[1] / obs_r;
    double up_z = obs_ecef[2] / obs_r;

    double dot_ru = rx * up_x + ry * up_y + rz * up_z;
    if (dist < 1e-5) {
        elevation = 90.0;
    } else {
        double sin_el = dot_ru / dist;
        sin_el = std::max(-1.0, std::min(1.0, sin_el));
        elevation = std::asin(sin_el) * TLE_RAD2DEG;
    }

    double lat_rad = obs_lat * TLE_DEG2RAD;
    double lon_rad = obs_lon * TLE_DEG2RAD;

    double east_x = -std::sin(lon_rad);
    double east_y = std::cos(lon_rad);
    double east_z = 0.0;

    double north_x = -std::sin(lat_rad) * std::cos(lon_rad);
    double north_y = -std::sin(lat_rad) * std::sin(lon_rad);
    double north_z = std::cos(lat_rad);

    double proj_east = rx * east_x + ry * east_y + rz * east_z;
    double proj_north = rx * north_x + ry * north_y + rz * north_z;

    azimuth = std::atan2(proj_east, proj_north) * TLE_RAD2DEG;
    if (azimuth < 0) azimuth += 360.0;
}

SatelliteState tle_calculate_state(TLE& tle,
                                   double obs_lat, double obs_lon, double obs_alt_km,
                                   double jd, double freq_hz) {
    SatelliteState state = {};

    double minutes_since_epoch = (jd - tle.rec.jdsatepoch - tle.rec.jdsatepochF) * 1440.0;

    double pos[3], vel[3];
    tle.getRV(minutes_since_epoch, pos, vel);

    state.pos_eci[0] = pos[0];
    state.pos_eci[1] = pos[1];
    state.pos_eci[2] = pos[2];
    state.vel_eci[0] = vel[0];
    state.vel_eci[1] = vel[1];
    state.vel_eci[2] = vel[2];
    state.speed_km_s = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);

    double gmst = tle_calc_gmst(jd);

    double obs_pos_eci[3];
    tle_observer_eci(obs_lat, obs_lon, obs_alt_km, gmst,
                     obs_pos_eci[0], obs_pos_eci[1], obs_pos_eci[2]);

    double obs_vel_eci[3];
    tle_observer_velocity_eci(obs_lat, obs_lon, obs_alt_km, gmst,
                              obs_vel_eci[0], obs_vel_eci[1], obs_vel_eci[2]);

    double dx = pos[0] - obs_pos_eci[0];
    double dy = pos[1] - obs_pos_eci[1];
    double dz = pos[2] - obs_pos_eci[2];
    state.range_km = std::sqrt(dx * dx + dy * dy + dz * dz);

    double sat_ecef[3], obs_ecef[3];
    tle_eci_to_ecef(pos[0], pos[1], pos[2], gmst, sat_ecef[0], sat_ecef[1], sat_ecef[2]);
    tle_eci_to_ecef(obs_pos_eci[0], obs_pos_eci[1], obs_pos_eci[2], gmst,
                    obs_ecef[0], obs_ecef[1], obs_ecef[2]);

    tle_ecef_to_lat_lon_alt(sat_ecef[0], sat_ecef[1], sat_ecef[2],
                            state.sat_lat, state.sat_lon, state.sat_alt_km);

    tle_calc_azimuth_elevation(obs_lat, obs_lon, obs_alt_km, sat_ecef, obs_ecef,
                               state.azimuth_deg, state.elevation_deg);

    state.occluded = tle_check_earth_occlusion(sat_ecef, obs_ecef);
    state.visible = (state.elevation_deg > 0.0) && (!state.occluded);

    double light_time_sec = state.range_km / TLE_C_KM_S;
    state.light_time_ms = light_time_sec * 1000.0;

    double sat_pos_tx[3] = {
        pos[0] - vel[0] * light_time_sec,
        pos[1] - vel[1] * light_time_sec,
        pos[2] - vel[2] * light_time_sec
    };

    double dx_tx = sat_pos_tx[0] - obs_pos_eci[0];
    double dy_tx = sat_pos_tx[1] - obs_pos_eci[1];
    double dz_tx = sat_pos_tx[2] - obs_pos_eci[2];
    double range_tx = std::sqrt(dx_tx * dx_tx + dy_tx * dy_tx + dz_tx * dz_tx);

    double v_rel_x = vel[0] - obs_vel_eci[0];
    double v_rel_y = vel[1] - obs_vel_eci[1];
    double v_rel_z = vel[2] - obs_vel_eci[2];

    if (range_tx > 0) {
        double u_rx = dx_tx / range_tx;
        double u_ry = dy_tx / range_tx;
        double u_rz = dz_tx / range_tx;
        state.v_radial_km_s = v_rel_x * u_rx + v_rel_y * u_ry + v_rel_z * u_rz;
        state.doppler_hz = -(freq_hz * state.v_radial_km_s) / TLE_C_KM_S;
    } else {
        state.v_radial_km_s = 0.0;
        state.doppler_hz = 0.0;
    }

    return state;
}
