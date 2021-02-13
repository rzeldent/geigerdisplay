#include <Arduino.h>

#include "OneButton.h"

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <EEPROM.h>

#include "SSD1306.h"
#include "SH1106.h"

#include "radiation_icon.h"

#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "LittleFS.h"

#include <vector>
#include <tuple>

// Inputs / outpunts
#define GPIO_OUT_SDA D1
#define GPIO_OUT_SCL D2

#define GPIO_IN_GEIGER D7
#define GPIO_IN_BUTTON D3

// Display settings 128x64 OLED
#define OLED_MAX_CX 128
#define OLED_MAX_CY 64

// CPM => uS/h
//#define CPM_USH_CONVERSION	0.006315	// SBM20
//#define CPM_USH_CONVERSION	0.010000	// SI29BG
//#define CPM_USH_CONVERSION	0.001500	// SBM19
//#define CPM_USH_CONVERSION	0.006666	// STS5
//#define CPM_USH_CONVERSION	0.001714	// SI22G
//#define CPM_USH_CONVERSION	0.631578	// SI3BG
//#define CPM_USH_CONVERSION	0.048000	// SBM21
//#define CPM_USH_CONVERSION	0.005940	// LND712
//#define CPM_USH_CONVERSION	0.010900	// SBT9
//#define CPM_USH_CONVERSION	0.006000	// SI1G
#define CPM_USH_CONVERSION (1.0 / 153.8) // M4011
#define TEXT_TUBE_TYPE "M4011"

// Text constants
#define TEXT_TUBE "GM Tube: "

#define TEXT_CPM "CPM"
#define TEXT_USH "uS/h"
#define TEXT_MAX "Max"
#define TEXT_AVG "Avg"
#define TEXT_TOTAL "Total exposure"

// Logging period in milliseconds, recommended value 15000-60000.
#define CPM_LOG_PERIOD_VERY_FAST 5000
#define CPM_LOG_PERIOD_FAST 15000
#define CPM_LOG_PERIOD_NORMAL 30000
#define CPM_LOG_PERIOD_SLOW 60000
#define CPM_LOG_PERIOD_VERY_SLOW 120000

// CPM period =  per minute (60 seconds)
#define CPM_PERIOD 60000

// Number of impulses
volatile unsigned long long impulses;
unsigned long long last_impulses;

// Last calculation
unsigned long last_time_cpm_calculated;
// CPM calculation interval
unsigned long log_period = CPM_LOG_PERIOD_FAST;
// CPM
unsigned long cpm;

#define MAX_HISTORY_REST 1000
std::vector<std::tuple<unsigned long, unsigned long long>> history;

#define MAX_HISTORY_DISPLAY OLED_MAX_CX
std::vector<unsigned long> history_cpm;
float avg_cpm;
unsigned long max_cpm;

// Webserver
#define WIFI_SSID "M4011"
#define WIFI_PASSWORD ""

DNSServer dnsServer;
AsyncWebServer server(80);

// Create display(Adr, SDA-pin, SCL-pin)
SSD1306 display(0x3c, GPIO_OUT_SDA, GPIO_OUT_SCL); // GPIO 5 = D1, GPIO 4 = D2

OneButton button(GPIO_IN_BUTTON, true);

enum display_mode_t
{
  display_cpm_graph_max,
  display_ush_graph_max,
  display_cpm_graph_avg,
  display_ush_graph_avg,
  display_cpm,
  display_ush,
  display_cpm_avg,
  display_ush_avg,
  display_cpm_act_log_gauge,
  display_ush_act_log_gauge,
  display_cpm_max_log_gauge,
  display_ush_max_log_gauge,
  display_cpm_avg_log_gauge,
  display_ush_avg_log_gauge,
  display_total_s,
  display_info
};

display_mode_t display_mode = display_ush_act_log_gauge;
bool redraw;

// ISR pulse from Geiger
void ICACHE_RAM_ATTR tube_impulse()
{
  impulses++;
}

void onButtonClick()
{
  switch (display_mode)
  {
  case display_cpm_graph_max:
    display_mode = display_ush_graph_max;
    break;

  case display_ush_graph_max:
    display_mode = display_cpm_graph_avg;
    break;

  case display_cpm_graph_avg:
    display_mode = display_ush_graph_avg;
    break;

  case display_ush_graph_avg:
    display_mode = display_cpm;
    break;

  case display_cpm:
    display_mode = display_ush;
    break;

  case display_ush:
    display_mode = display_cpm_avg;
    break;

  case display_cpm_avg:
    display_mode = display_ush_avg;
    break;

  case display_ush_avg:
    display_mode = display_cpm_act_log_gauge;
    break;

  case display_cpm_act_log_gauge:
    display_mode = display_ush_act_log_gauge;
    break;

  case display_ush_act_log_gauge:
    display_mode = display_cpm_max_log_gauge;
    break;

  case display_cpm_max_log_gauge:
    display_mode = display_ush_max_log_gauge;
    break;

  case display_ush_max_log_gauge:
    display_mode = display_cpm_avg_log_gauge;
    break;

  case display_cpm_avg_log_gauge:
    display_mode = display_ush_avg_log_gauge;
    break;

  case display_ush_avg_log_gauge:
    display_mode = display_total_s;
    break;

  case display_total_s:
    display_mode = display_info;
    break;

  case display_info:
    display_mode = display_cpm_graph_max;
    break;
  }

  redraw = true;
}

void onButtonDoubleClick()
{
  String text;

  switch (log_period)
  {
  case CPM_LOG_PERIOD_VERY_FAST:
    log_period = CPM_LOG_PERIOD_FAST;
    text = "FAST";
    break;

  case CPM_LOG_PERIOD_FAST:
    log_period = CPM_LOG_PERIOD_NORMAL;
    text = "NORMAL";
    break;

  case CPM_LOG_PERIOD_NORMAL:
    log_period = CPM_LOG_PERIOD_SLOW;
    text = "SLOW";
    break;

  case CPM_LOG_PERIOD_SLOW:
    log_period = CPM_LOG_PERIOD_VERY_SLOW;
    text = "VERY SLOW";
    break;

  case CPM_LOG_PERIOD_VERY_SLOW:
    log_period = CPM_LOG_PERIOD_VERY_FAST;
    text = "VERY FAST";
    break;
  }

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(OLED_MAX_CX / 2, 0, "Update speed");
  display.setFont(ArialMT_Plain_16);
  display.drawString(OLED_MAX_CX / 2, 16, text);
  display.setFont(ArialMT_Plain_10);
  display.drawString(OLED_MAX_CX / 2, 40, "Interval: " + String(log_period / 1000) + " sec.");
  display.drawString(OLED_MAX_CX / 2, 54, "Wait...");
  display.display();

  last_time_cpm_calculated = millis();
  last_impulses = impulses;
}

char *ul64toa(uint64_t value)
{
  static char result[21];
  memset(result, 0, sizeof(result));
  while (value)
  {
    memmove(&result[1], result, sizeof(result) - 1);
    result[0] = '0' + value % 10;
    value /= 10;
  }

  return result;
}

void setup()
{
  // put your setup code here, to run once:
  WiFi.mode(WIFI_AP);

  // Start Serial
  Serial.begin(115200);

  if (!LittleFS.begin())
    Serial.println("LITTLEFS Mount Failed");

  // Start Display
  display.init();
  display.flipScreenVertically();

  // Show start screen
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "Geiger");
  display.drawString(0, 16, "Display");
  display.drawXbm(128 - 32, 2, 32, 32, radiation_icon);

  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(OLED_MAX_CX / 2, 40, "Copyright (c) 2021");
  display.drawString(OLED_MAX_CX / 2, 52, "Rene Zeldenthuis");
  display.display();

  // Link the onButtonClick function to be called on a click event.
  button.attachClick(onButtonClick);
  button.attachDoubleClick(onButtonDoubleClick);

  // Define external interrupts
  pinMode(GPIO_IN_GEIGER, INPUT);
  attachInterrupt(digitalPinToInterrupt(GPIO_IN_GEIGER), tube_impulse, FALLING);

  // Wifi Access Point mode
  if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD))
    Serial.println("Creating access point failed");

  dnsServer.start(53, "*", WiFi.softAPIP());
  MDNS.addService("http", "tcp", 80);

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/favicon.ico", "image/x-icon");
  });
  server.on("/jquery-3.5.1.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/jquery-3.5.1.min.js", "text/javascript");
  });
  server.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/bootstrap.min.css", "text/css");
  });
  server.on("/q", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Return the REST response
    auto json = "{cpm_to_ush:" + String(CPM_USH_CONVERSION, 10) + "," +
                "counts:[";
    for (auto it = history.begin(); it != history.end(); ++it)
    {
      if (it != history.begin())
        json += ",";

      json += "{time:" + String(std::get<0>(*it) / 1000) + ",count:" + ul64toa(std::get<1>(*it)) + "}";
    }
    json += "]}";
    Serial.println(json);
    request->send(200, "application/json", json);
  });

  server.begin();
}

String format_value(const float value)
{
  // No decimal places
  if (value >= 1)
    return String(value, 0);
  if (value < 0.001f)
    return String(value, 4);
  if (value < 0.01f)
    return String(value, 3);
  if (value < 0.1f)
    return String(value, 2);
  return String(value, 1);
}

String format_si(const double value)
{
  if (value == 0.0)
    return "0";
  auto value_abs = fabs(value);
  if (value_abs < 1E-9)
    return String(value * 1E9, 2) + "p";
  if (value_abs < 1E-6)
    return String(value * 1E9, 2) + "n";
  if (value_abs < 1E-3)
    return String(value * 1E6, 2) + "u";
  if (value_abs < 0)
    return String(value * 1E3, 2) + "m";
  if (value_abs < 1E3)
    return String(value, 2);
  if (value_abs < 1E6)
    return String(value / 1E3, 2) + "M";
  if (value_abs < 1E9)
    return String(value / 1E6, 2) + "G";
  if (value_abs < 1E12)
    return String(value / 1E9, 2) + "T";
  return "NaN";
}

String format_d_h_m_s(ulong seconds)
{
  auto days = seconds / (60 * 60 * 24);
  seconds %= (60 * 60 * 24);
  auto hours = seconds / (60 * 60);
  seconds %= 60 * 60;
  auto minutes = seconds / 60;
  seconds %= 60;
  return String(days) + " " + (hours < 10 ? "0" : "") + String(hours) + ((minutes < 10 ? ":0" : ":") + String(minutes) + (seconds < 10 ? ":0" : ":") + String(seconds));
}

void display_meter(const std::vector<float> &scale, const char *units, const char *type, float value)
{
  // x_center is a little bit off (2px) to the left to accomodate large values on the right
  const auto x_center = OLED_MAX_CX / 2 - 2;
  const auto y_center = OLED_MAX_CY - 11;
  const auto r = OLED_MAX_CX / 2 - 25;
  const auto r_center = 2;
  const auto r_tick = r + 3;
  const auto r_text = r + 15;
  const auto r_gauge = r - 4;

  display.clear();
  display.setFont(ArialMT_Plain_10);

  display.drawCircle(x_center, y_center, r_center);
  // Draw border of the gauge
  display.drawCircleQuads(x_center, y_center, r, 0b0011);
  // Draw the units
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, units);
  // Draw the type
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(OLED_MAX_CX, 0, type);

  display.setTextAlignment(TEXT_ALIGN_CENTER);

  auto const min_tick = *std::min_element(scale.begin(), scale.end());
  auto const max_tick = *std::max_element(scale.begin(), scale.end());
  auto const log_min = log(min_tick);
  auto const log_scale_minmax = log(max_tick) - log_min;

  // Draw the ticks 0.1 - 100 (-1 => 3)
  for (auto tick : scale)
  {
    // Log values are from -1 to 2 => correct to 0 -> 3
    auto const log_tick = log(tick) - log_min;
    // Convert to radians (PI is half a circle)
    auto const tick_radians = log_tick / log_scale_minmax * PI;

    auto const tick_sin = sin(tick_radians);
    auto const tick_cos = cos(tick_radians);
    // tick is on circle:
    display.drawLine(x_center - r * tick_cos, y_center - r * tick_sin, x_center - r_tick * tick_cos, y_center - r_tick * tick_sin);

    display.drawString(x_center - r_text * tick_cos, y_center - r_text * tick_sin, format_value(tick));
  }

  // Draw value
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x_center, OLED_MAX_CY - 10, format_value(value));

  // Draw the gauge
  if (value < min_tick)
    value = min_tick;
  else if (value > max_tick)
    value = max_tick;

  auto const log_value = log(value) - log_min;
  // Convert to radians (PI is half a circle)
  auto const value_radians = log_value / log_scale_minmax * PI;
  display.drawLine(x_center, y_center, x_center - r_gauge * cos(value_radians), y_center - r_gauge * sin(value_radians));

  display.display();
}

void display_history_graph(unsigned long max_cpm)
{
  if (history_cpm.size())
  {
    // Display a history graph. First line reserved for text
    const float max_height = (OLED_MAX_CY - 10) - 1;
    const auto cpm_adjust = max_cpm ? max_height / max_cpm : 0.0;
    int16_t x = 0;
    for (auto it : history_cpm)
    {
      // Draw from the bottom up
      display.drawLine(x, OLED_MAX_CY, x, OLED_MAX_CY - it * cpm_adjust);
      x++;
    }
  }
}

void loop()
{
  // put your main code here, to run repeatedly:

  // Wifi / Web
  dnsServer.processNextRequest();

  // keep watching the push button
  button.tick();

  // Calculate the CPM
  auto now = millis();
  if (now - last_time_cpm_calculated > log_period)
  {
    // Make sure not updated while calculating
    noInterrupts();
    auto impulses_diff = impulses - last_impulses;
    last_impulses = impulses;
    interrupts();

    last_time_cpm_calculated = now;
    cpm = impulses_diff * CPM_PERIOD / log_period;

    // Update histories
    history.insert(history.begin(), std::tuple<unsigned long, unsigned long long>{now, last_impulses});
    if (history.size() > MAX_HISTORY_REST)
      history.pop_back();

    history_cpm.insert(history_cpm.begin(), cpm);
    if (history_cpm.size() > MAX_HISTORY_DISPLAY)
      history_cpm.pop_back();

    // Calculate average
    const auto history_oldest = history.back();
    const auto impulses = last_impulses - std::get<1>(history_oldest);
    const auto milli_seconds = now - std::get<0>(history_oldest);
    avg_cpm = milli_seconds ? impulses * CPM_PERIOD / milli_seconds : 0.0f;

    // Update maximum CPM
    if (cpm > max_cpm)
      max_cpm = cpm;

    Serial.println(cpm);

    redraw = true;
  }

  if (redraw)
  {
    redraw = false;
    display.clear();

    static const std::vector<float> scale_cpm = {10.0f, 20.0f, 30.0f, 50.0f, 200.0f, 300.0f, 500.0f, 1000.0f};
    static const std::vector<float> scale_ush = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f};

    switch (display_mode)
    {
    case display_cpm_graph_max:
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 0, String(TEXT_CPM ": ") + String(cpm));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(OLED_MAX_CX, 0, String(TEXT_MAX ": ") + String(max_cpm));
      display_history_graph(max_cpm);
      break;

    case display_ush_graph_max:
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 0, String(TEXT_USH ": ") + format_value(cpm * CPM_USH_CONVERSION));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(OLED_MAX_CX, 0, String(TEXT_MAX ": ") + format_value(max_cpm * CPM_USH_CONVERSION));
      display_history_graph(max_cpm);
      break;

    case display_cpm_graph_avg:
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 0, String(TEXT_CPM ": ") + String(cpm));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(OLED_MAX_CX, 0, String(TEXT_AVG ": ") + format_value(avg_cpm));
      display_history_graph(max_cpm);
      break;

    case display_ush_graph_avg:
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 0, String(TEXT_USH ": ") + format_value(cpm * CPM_USH_CONVERSION));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(OLED_MAX_CX, 0, String(TEXT_AVG ": ") + format_value(avg_cpm * CPM_USH_CONVERSION));
      display_history_graph(max_cpm);
      break;

    case display_cpm:
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(OLED_MAX_CX / 2, 0, TEXT_CPM);
      display.setFont(ArialMT_Plain_24);
      display.drawString(OLED_MAX_CX / 2, 24, String(cpm));
      break;

    case display_ush:
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(OLED_MAX_CX / 2, 0, TEXT_USH);
      display.setFont(ArialMT_Plain_24);
      display.drawString(OLED_MAX_CX / 2, 24, format_value(cpm * CPM_USH_CONVERSION));
      break;

    case display_cpm_avg:
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(OLED_MAX_CX / 2, 0, TEXT_CPM " " TEXT_AVG);
      display.setFont(ArialMT_Plain_24);
      display.drawString(OLED_MAX_CX / 2, 24, format_value(avg_cpm));
      break;

    case display_ush_avg:
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(OLED_MAX_CX / 2, 0, TEXT_USH " " TEXT_AVG);
      display.setFont(ArialMT_Plain_24);
      display.drawString(OLED_MAX_CX / 2, 24, format_value(avg_cpm * CPM_USH_CONVERSION));
      break;

    case display_cpm_act_log_gauge:
      display_meter(scale_cpm, TEXT_CPM, "", cpm);
      break;

    case display_ush_act_log_gauge:
      display_meter(scale_ush, TEXT_USH, "", cpm * CPM_USH_CONVERSION);
      break;

    case display_cpm_max_log_gauge:
      display_meter(scale_cpm, TEXT_CPM, TEXT_MAX, max_cpm);
      break;

    case display_ush_max_log_gauge:
      display_meter(scale_ush, TEXT_USH, TEXT_MAX, max_cpm * CPM_USH_CONVERSION);
      break;

    case display_cpm_avg_log_gauge:
      display_meter(scale_cpm, TEXT_CPM, TEXT_AVG, avg_cpm);
      break;

    case display_ush_avg_log_gauge:
      display_meter(scale_ush, TEXT_USH, TEXT_AVG, avg_cpm * CPM_USH_CONVERSION);
      break;

    case display_total_s:
      // Calculate total in Sieverts
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(OLED_MAX_CX / 2, 0, TEXT_TOTAL);
      display.setFont(ArialMT_Plain_24);

      display.drawString(OLED_MAX_CX / 2, 24, format_si(impulses * CPM_USH_CONVERSION * 60 / 1E6) + "S");
      display.setFont(ArialMT_Plain_10);
      display.drawString(OLED_MAX_CX / 2, 54, format_d_h_m_s(millis() / 1000));
      // Can be done continuously
      redraw = true;
      break;

    case display_info:
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 0, TEXT_TUBE TEXT_TUBE_TYPE);
      display.drawXbm(48, 20, 32, 32, radiation_icon);
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 54, TEXT_CPM " to " TEXT_USH ": " + String(CPM_USH_CONVERSION, 6));
      break;
    }

    display.display();
  }
}