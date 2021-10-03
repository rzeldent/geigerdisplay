#include <Arduino.h>

#include "soc/rtc_cntl_reg.h"
#include <SPI.h>
// Setting for the display are defined in platformio.ini
#include <TFT_eSPI.h>

#define TFT_BL 4 // Display backlight control pin

#define FONT_10PT 2
#define FONT_26PT 4
#define FONT_48PT 6
#define FONT_48PT_LCD 7

#include <Button2.h>

#include <WiFi.h>
//#include <DNSServer.h>
//#include <ESP8266mDNS.h>
//#include <ESPAsyncTCP.h>
//#include <ESPAsyncWebServer.h>

//#include "LittleFS.h"
#include <images.h>

#include <vector>
#include <tuple>

#define GPIO_IN_GEIGER 27

#define BUTTON_1 35
#define BUTTON_2 0

// Screen is 240 * 135 pixels (rotated)
#define DISPLAY_MAX_CX 240
#define DISPLAY_MAX_CY 135
#define BACKGROUND_COLOR TFT_BLACK

#define COLOR_TEXT TFT_WHITE

#define COLOR_SCALE TFT_BLUE
#define COLOR_TICK TFT_YELLOW
#define COLOR_GAUGE TFT_RED

#define COLOR_GRAPH TFT_VIOLET

// Use hardware SPI
auto tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);

Button2 button1(BUTTON_1, INPUT);
Button2 button2(BUTTON_2, INPUT);

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
#define TEXT_MAX "MAX"
#define TEXT_AVG "AVG"
#define TEXT_TOTAL "Total exposure"
#define TEXT_CONVERSION "Conversion: "
#define TEXT_INPUT_PIN "GPIO input: "

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
// Tuple: <time, impulses>
std::vector<std::tuple<unsigned long, unsigned long long>> history;

#define MAX_HISTORY_DISPLAY DISPLAY_MAX_CX
std::vector<unsigned long> history_cpm;
float avg_cpm;
unsigned long max_cpm;

// Webserver
#define WIFI_SSID "M4011"
#define WIFI_PASSWORD ""

//DNSServer dnsServer;
//AsyncWebServer server(80);

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

void change_display_mode(const Button2 &button)
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

void change_update_speed(const Button2 &button)
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

  // Clear the screen
  tft.fillRect(0, 0, TFT_HEIGHT, TFT_WIDTH, BACKGROUND_COLOR);
  tft.drawCentreString("Update speed", DISPLAY_MAX_CX / 2, 0, FONT_10PT);
  tft.drawCentreString(text, DISPLAY_MAX_CX / 2, 20, FONT_26PT);
  tft.drawCentreString("Interval: " + String(log_period / 1000) + " sec.", DISPLAY_MAX_CX / 2, 46, FONT_26PT);
  tft.drawCentreString("Wait...", DISPLAY_MAX_CX / 2, DISPLAY_MAX_CY - 16, FONT_10PT);

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
  // Disable brownout
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // put your setup code here, to run once:
  WiFi.mode(WIFI_AP);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  esp_log_level_set("*", ESP_LOG_VERBOSE);

  log_i("CPU Freq = %d Mhz", getCpuFrequencyMhz());
  log_i("Starting Geiger display...");

  // if (!LittleFS.begin())
  //   log_e("LittleFS Mount Failed");

  // Define external interrupts
  pinMode(GPIO_IN_GEIGER, INPUT);
  attachInterrupt(digitalPinToInterrupt(GPIO_IN_GEIGER), tube_impulse, FALLING);

  // Start Display
  tft.init();
#ifdef TFT_BL
  // TFT_BL has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
  pinMode(TFT_BL, OUTPUT);                // Set backlight pin to output mode
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON); // Turn backlight on. TFT_BACKLIGHT_ON has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
#endif

  tft.setSwapBytes(true); // Swap the byte order for pushImage() - corrects endianness
  tft.setRotation(1);
  tft.setTextDatum(TL_DATUM); // Top Left
  tft.setTextColor(COLOR_TEXT);
  tft.setTextWrap(false, false);

  // Clear the screen
  tft.fillRect(0, 0, TFT_HEIGHT, TFT_WIDTH, BACKGROUND_COLOR);
  tft.setCursor(0, 0);
  // Show image
  tft.pushImage(0, 0, image_splash.width, image_splash.height, image_splash.data);

  // Link the onButtonClick function to be called on a click event.
  button1.setPressedHandler(change_display_mode);
  button2.setPressedHandler(change_update_speed);

  tft.setTextColor(COLOR_TEXT);
  /*
  // Wifi Access Point mode
  if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD))
    log_e("Creating access point failed");

  dnsServer.start(53, "*", WiFi.softAPIP());
  MDNS.addService("http", "tcp", 80);

  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->redirect("/"); });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html"); });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/favicon.ico", "image/x-icon"); });
  server.on("/jquery-3.5.1.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/jquery-3.5.1.min.js", "text/javascript"); });
  server.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/bootstrap.min.css", "text/css"); });
  server.on("/q", HTTP_GET, [](AsyncWebServerRequest *request)
            {
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
              log_i(json);
              request->send(200, "application/json", json);
            });

  server.begin();
  */
}

String format_value(float value)
{
  // No decimal places
  if (value < 0)
    return "-" + format_value(-value);

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

String format_si(double value, byte decimal_places)
{
  if (value == 0.0)
    return "0";

  if (value < 0)
    return "-" + format_si(-value, decimal_places);

  auto value_abs = fabs(value);
  if (value_abs < 1E-9)
    return String(value * 1E9, decimal_places) + "p";
  if (value_abs < 1E-6)
    return String(value * 1E9, decimal_places) + "n";
  if (value_abs < 1E-3)
    return String(value * 1E6, decimal_places) + "u";
  if (value_abs < 1)
    return String(value * 1E3, decimal_places) + "m";
  if (value_abs < 1E3)
    return String(value, 2);
  if (value_abs < 1E6)
    return String(value / 1E3, decimal_places) + "M";
  if (value_abs < 1E9)
    return String(value / 1E6, decimal_places) + "G";
  if (value_abs < 1E12)
    return String(value / 1E9, decimal_places) + "T";
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
  return String(days) + " days, " + (hours < 10 ? "0" : "") + String(hours) + ((minutes < 10 ? ":0" : ":") + String(minutes) + (seconds < 10 ? ":0" : ":") + String(seconds));
}

void display_meter(const std::vector<float> &scale, const char *units, const char *type, float value)
{
  // x_center is a little bit off (2px) to the left to accommodate large values on the right
  const auto x_center = DISPLAY_MAX_CX / 2;
  const auto y_center = DISPLAY_MAX_CY - 20;
  const auto r = DISPLAY_MAX_CX / 2 - 25;
  const auto r_center = 2;
  const auto r_tick = r + 3;
  const auto r_text = r + 15;
  const auto r_gauge = r - 4;

  tft.fillRect(0, 0, TFT_HEIGHT, TFT_WIDTH, BACKGROUND_COLOR);
  tft.setCursor(0, 0);

  tft.drawCircle(x_center, y_center, r_center, COLOR_SCALE);
  // Draw border of the gauge
  tft.drawCircleHelper(x_center, y_center, r, 0b0011, COLOR_SCALE);
  // Draw the units
  tft.drawString(units, 0, 0, FONT_10PT);
  // Draw the type
  tft.drawRightString(type, DISPLAY_MAX_CX, 0, FONT_10PT);

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
    tft.drawLine(x_center - r * tick_cos, y_center - r * tick_sin, x_center - r_tick * tick_cos, y_center - r_tick * tick_sin, COLOR_TICK);
    tft.drawCentreString(format_value(tick), x_center - r_text * tick_cos, y_center - r_text * tick_sin, FONT_10PT);
  }

  // Draw value
  tft.drawCentreString(format_value(value), x_center, y_center, FONT_10PT);

  // Draw the gauge
  if (value < min_tick)
    value = min_tick;
  else if (value > max_tick)
    value = max_tick;

  auto const log_value = log(value) - log_min;
  // Convert to radians (PI is half a circle)
  auto const value_radians = log_value / log_scale_minmax * PI;
  tft.drawLine(x_center, y_center, x_center - r_gauge * cos(value_radians), y_center - r_gauge * sin(value_radians), COLOR_GAUGE);
}

void display_history_graph(unsigned long max_cpm)
{
  if (history_cpm.size())
  {
    // Display a history graph. First line reserved for text
    const float max_height = (DISPLAY_MAX_CY - 10) - 1;
    const auto cpm_adjust = max_cpm ? max_height / max_cpm : 0.0;
    int16_t x = 0;
    for (auto it : history_cpm)
    {
      // Draw from the bottom up
      tft.drawLine(x, DISPLAY_MAX_CY, x, DISPLAY_MAX_CY - it * cpm_adjust, COLOR_GRAPH);
      x++;
    }
  }
}

void loop()
{
  // put your main code here, to run repeatedly:

  // WiFi / Web
  //dnsServer.processNextRequest();

  // keep watching the push buttons
  button1.loop();
  button2.loop();

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

    log_i("CPM: %ld", cpm);

    redraw = true;
  }

  if (redraw)
  {
    redraw = false;
    tft.fillRect(0, 0, TFT_HEIGHT, TFT_WIDTH, BACKGROUND_COLOR);
    tft.setCursor(0, 0);

    static const std::vector<float> scale_cpm = {10.0f, 20.0f, 30.0f, 50.0f, 100.0f, 200.0f, 300.0f, 500.0f, 1000.0f};
    static const std::vector<float> scale_ush = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f};

    switch (display_mode)
    {
    case display_cpm_graph_max:
      tft.drawString(String(TEXT_CPM ": ") + String(cpm), 0, 0, FONT_10PT);
      tft.drawRightString(String(TEXT_MAX ": ") + String(max_cpm), DISPLAY_MAX_CX, 0, FONT_10PT);
      display_history_graph(max_cpm);
      break;

    case display_ush_graph_max:
      tft.drawString(String(TEXT_USH ": ") + format_value(cpm * CPM_USH_CONVERSION), 0, 0, FONT_10PT);
      tft.drawRightString(String(TEXT_MAX ": ") + format_value(max_cpm * CPM_USH_CONVERSION), DISPLAY_MAX_CX, 0, FONT_10PT);
      display_history_graph(max_cpm);
      break;

    case display_cpm_graph_avg:
      tft.drawString(String(TEXT_CPM ": ") + String(cpm), 0, 0, FONT_10PT);
      tft.drawRightString(String(TEXT_AVG ": ") + format_value(avg_cpm), DISPLAY_MAX_CX, 0, FONT_10PT);
      display_history_graph(max_cpm);
      break;

    case display_ush_graph_avg:
      tft.drawString(String(TEXT_USH ": ") + format_value(cpm * CPM_USH_CONVERSION), 0, 0, FONT_10PT);
      tft.drawRightString(String(TEXT_AVG ": ") + format_value(avg_cpm * CPM_USH_CONVERSION), DISPLAY_MAX_CX, 0, FONT_10PT);
      display_history_graph(max_cpm);
      break;

    case display_cpm:
      tft.drawCentreString(TEXT_CPM, DISPLAY_MAX_CX / 2, 0, FONT_10PT);
      tft.drawCentreString(String(cpm), DISPLAY_MAX_CX / 2, (DISPLAY_MAX_CY - 48) / 2, FONT_48PT_LCD);
      break;

    case display_ush:
      tft.drawCentreString(TEXT_USH, DISPLAY_MAX_CX / 2, 0, FONT_10PT);
      tft.drawCentreString(format_value(cpm * CPM_USH_CONVERSION), DISPLAY_MAX_CX / 2, (DISPLAY_MAX_CY - 48) / 2, FONT_48PT_LCD);
      break;

    case display_cpm_avg:
      tft.drawCentreString(TEXT_CPM " " TEXT_AVG, DISPLAY_MAX_CX / 2, 0, FONT_10PT);
      tft.drawCentreString(format_value(avg_cpm), DISPLAY_MAX_CX / 2, (DISPLAY_MAX_CY - 48) / 2, FONT_48PT_LCD);
      break;

    case display_ush_avg:
      tft.drawCentreString(TEXT_USH " " TEXT_AVG, DISPLAY_MAX_CX / 2, 0, FONT_10PT);
      tft.drawString(format_value(avg_cpm * CPM_USH_CONVERSION), DISPLAY_MAX_CX / 2, (DISPLAY_MAX_CY - 48) / 2, FONT_48PT_LCD);
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
      tft.drawCentreString(TEXT_TOTAL, DISPLAY_MAX_CX / 2, 0, FONT_10PT);
      tft.drawCentreString(format_si(impulses * CPM_USH_CONVERSION * 60 / 1E6, 2) + "S", DISPLAY_MAX_CX / 2, 24, FONT_26PT);
      tft.drawCentreString(format_d_h_m_s(millis() / 1000), DISPLAY_MAX_CX / 2, 54, FONT_10PT);
      break;

    case display_info:
      tft.setTextColor(TFT_YELLOW);
      tft.drawCentreString("Geiger Display", DISPLAY_MAX_CX / 2, 0, FONT_26PT);
      tft.setTextColor(TFT_LIGHTGREY);
      tft.drawCentreString("by Rene Zeldenthuis", DISPLAY_MAX_CX / 2, 28, FONT_10PT);
      tft.setTextColor(COLOR_TEXT);
      tft.drawString(TEXT_INPUT_PIN, 0, 70, FONT_10PT);
      tft.drawString(String(GPIO_IN_GEIGER), 80, 70, FONT_10PT);
      tft.drawString(TEXT_TUBE, 0, 84, FONT_10PT);
      tft.drawString(TEXT_TUBE_TYPE, 80, 84, FONT_10PT);
      tft.drawString(TEXT_CONVERSION, 0, 98, FONT_10PT);
      tft.drawString(format_si(CPM_USH_CONVERSION, 6) + " " + TEXT_CPM "/" TEXT_USH, 80, 98, FONT_10PT);

      break;
    }
  }
}