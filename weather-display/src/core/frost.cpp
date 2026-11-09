/**
 * @file    frost.cpp
 * @brief   Frost/ice risk rules.
 */
#include "frost.h"

namespace {

float dewPoint(float t, float rh) {
  if (rh <= 0) return NAN;
  const float a = 17.625f, b = 243.04f, g = logf(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

}  // namespace

namespace Frost {

FrostRisk assess(const WeatherData &wx, const TempestData &tp) {
  FrostRisk r;
  const bool live = tp.fresh();
  if (!live && !wx.valid) {
    snprintf(r.headline, sizeof(r.headline), "No data");
    return r;
  }
  const float t = live ? tp.obs.tempC : wx.current.temp;
  const float dew = live ? tp.dewPointC : dewPoint(wx.current.temp, wx.current.humidity);
  const bool wet = live ? (tp.rainHourMm > 0.05f || tp.rainTodayMm > 0.2f) : wx.current.precip > 0.05f;

  // Coldest forecast hour in the next 12 hours.
  for (int i = 1; i < wx.hourlyCount && i <= 12; ++i) {
    if (isnan(r.forecastMin) || wx.hourly[i].temp < r.forecastMin) {
      r.forecastMin = wx.hourly[i].temp;
      r.forecastMinHour = wx.hourly[i].hour;
    }
  }

  auto set = [&](FrostLevel l, const char *head) {
    r.level = l;
    strlcpy(r.headline, head, sizeof(r.headline));
  };
  if (t <= 0.0f && wet) set(FrostLevel::High, "Ice on wet surfaces");
  else if (t <= 0.0f) set(FrostLevel::High, "Frost now");
  else if (t <= 2.0f && wet) set(FrostLevel::High, "Ice risk: wet and near 0");
  else if (t <= 3.0f && !isnan(dew) && dew <= 0.0f) set(FrostLevel::Moderate, "Ground frost likely");
  else if (!isnan(r.forecastMin) && r.forecastMin <= 0.0f) set(FrostLevel::Moderate, "Frost expected");
  else if (!isnan(r.forecastMin) && r.forecastMin <= 3.0f) set(FrostLevel::Low, "Ground frost possible");

  if (isnan(r.forecastMin)) {
    snprintf(r.detail, sizeof(r.detail), "Air %.1f C, dew point %.1f C", t, dew);
  } else {
    snprintf(r.detail, sizeof(r.detail), "Air %.1f C, dew %.1f C, low %.0f C at %02u:00", t, dew, r.forecastMin,
             r.forecastMinHour);
  }
  return r;
}

const char *levelName(FrostLevel l) {
  switch (l) {
    case FrostLevel::Low: return "Low";
    case FrostLevel::Moderate: return "Moderate";
    case FrostLevel::High: return "High";
    default: return "None";
  }
}

}  // namespace Frost
