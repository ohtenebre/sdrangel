#pragma once

#include <string>
#include <vector>
#include <utility>

#include "SGP4.h"
#include "TLE.h"

constexpr double TLE_PI = 3.14159265358979323846;
constexpr double TLE_WGS84_A = 6378.137;
constexpr double TLE_WGS84_F = 1.0 / 298.257223563;
constexpr double TLE_WGS84_E2 = 2.0 * TLE_WGS84_F - TLE_WGS84_F * TLE_WGS84_F;
constexpr double TLE_C_KM_S = 299792.458;
constexpr double TLE_OMEGA_EARTH = 7.2921159e-5;
constexpr double TLE_MAX_AGE_DAYS = 14.0;
constexpr double TLE_DEG2RAD = TLE_PI / 180.0;
constexpr double TLE_RAD2DEG = 180.0 / TLE_PI;

struct TleData {
    std::string line1;
    std::string line2;
    std::string name;
    std::string norad_id;
    bool valid = false;
};

struct SatelliteState {
    double pos_eci[3];
    double vel_eci[3];
    double range_km;
    double elevation_deg;
    double azimuth_deg;
    double doppler_hz;
    double v_radial_km_s;
    double sat_lat;
    double sat_lon;
    double sat_alt_km;
    double speed_km_s;
    bool visible;
    bool occluded;
    double light_time_ms;
};

std::string tle_trim(const std::string& s);
std::string tle_extract_norad_id(const std::string& tle1);
std::pair<bool, std::string> tle_check_freshness(const std::string& tle1);

std::vector<TleData> tle_load_file(const std::string& filename);
TleData tle_load_single(const std::string& line1, const std::string& line2, const std::string& name);

double tle_calc_gmst(double jd);

void tle_eci_to_ecef(double x, double y, double z, double gmst,
                     double& x_out, double& y_out, double& z_out);
void tle_ecef_to_lat_lon_alt(double x, double y, double z,
                             double& lat, double& lon, double& alt);
void tle_observer_ecef(double lat_deg, double lon_deg, double alt_km,
                       double& x, double& y, double& z);
void tle_observer_eci(double lat_deg, double lon_deg, double alt_km, double gmst,
                      double& x, double& y, double& z);
void tle_observer_velocity_eci(double lat_deg, double lon_deg, double alt_km, double gmst,
                               double& vx, double& vy, double& vz);
bool tle_check_earth_occlusion(const double sat_ecef[3], const double obs_ecef[3]);
void tle_calc_azimuth_elevation(double obs_lat, double obs_lon, double obs_alt_km,
                                const double sat_ecef[3], const double obs_ecef[3],
                                double& azimuth, double& elevation);

SatelliteState tle_calculate_state(TLE& tle,
                                   double obs_lat, double obs_lon, double obs_alt_km,
                                   double jd, double freq_hz);
