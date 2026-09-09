#pragma once
#include <stdint.h>

// State mesin UI
enum class UIState : uint8_t{
  DASHBOARD,
  MENU,
  WIFI_MGR,
  CALIB,
  CAL_DO,
  CAL_PH,
  CAL_WIZARD
};

// Snapshot data yang dikirim dari TaskSensors ke TaskUI/TaskHTTP
struct DisplayData {
  float water_temp;
  float air_temp;
  float ph, phT;
  float do_mgL, do_tC;
  float humidity;
  float lux;

  bool ph_ok;
  bool do_ok;
  bool env_ok;

  float bat_v;             // Tegangan baterai (V)
  float bat_pct;           // Persentase baterai (0-100)
  bool  bat_ok;            // true kalau pembacaan baterai valid

  bool  wifiOK;            // Status WiFi
  int8_t wifiStatus;       // Kode wl_status_t (0-6)
  char  postStatus[16];    // Status HTTP POST terakhir ("OK", "E500", dsb)
};
