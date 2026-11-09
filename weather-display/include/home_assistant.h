/**
 * @file    home_assistant.h
 * @brief   Home Assistant summary via its REST template API.
 *
 * @details Instead of downloading every entity (/api/states can be hundreds of
 *          KB), one POST to `/api/template` asks Home Assistant to render a
 *          compact JSON summary server-side:
 *          - temperature, humidity and power sensors (numeric states only)
 *          - people (home / away / zone)
 *          - climate entities (current/target temperature, action)
 *          - lights on / total
 *          - open doors, windows and garage doors
 *
 *          Needs a long-lived access token: Home Assistant -> your profile ->
 *          Security -> Long-lived access tokens -> Create. Enter it on the
 *          device's web settings page (http://<host>.local/settings), where it
 *          is write-only, or as a build-time default in include/secrets.h.
 */
#pragma once

#include <Arduino.h>

/** @brief A named numeric reading. */
struct HaReading {
  char name[28] = "";
  float value = 0;
  char unit[8] = "";
};

/** @brief A person entity. */
struct HaPerson {
  char name[20] = "";
  char state[20] = "";  ///< "home", "not_home" or a zone name.
};

/** @brief A climate (thermostat/heating) entity. */
struct HaClimate {
  char name[24] = "";
  float current = NAN;
  float target = NAN;
  char action[16] = "";  ///< heating, idle, off...
};

/** @brief Summary of the home. */
struct HaData {
  bool configured = false;   ///< A token is compiled in.
  bool valid = false;        ///< Last request succeeded.
  int lastStatus = 0;        ///< HTTP status of the last request (401 = bad token).
  uint32_t fetchedAtMs = 0;
  HaReading temps[16];
  uint8_t tempCount = 0;
  HaReading humidity[12];
  uint8_t humidityCount = 0;
  HaReading power[12];       ///< Highest first.
  uint8_t powerCount = 0;
  HaPerson people[8];
  uint8_t peopleCount = 0;
  HaClimate climate[3];
  uint8_t climateCount = 0;
  uint16_t lightsOn = 0, lightsTotal = 0;
  char open[8][28] = {};     ///< Open doors/windows.
  uint8_t openCount = 0;
};

namespace HomeAssistant {

/** @brief True if a token is configured (settings page or secrets.h). */
bool configured();

/** @brief Request the summary (no-op returning false when not configured). */
bool fetch(HaData &out);

}  // namespace HomeAssistant
