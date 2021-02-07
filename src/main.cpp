#include <Arduino.h>

#include "OneButton.h"

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <EEPROM.h>

#include "SSD1306.h"
#include "SH1106.h"

#include "radiation_icon.h"

// Inputs / outpunts
#define GPIO_OUT_SDA D1
#define GPIO_OUT_SCL D2

#define GPIO_IN_GEIGER D7
#define GPIO_IN_BUTTON D3

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

static String tube_text("M4011");

// Logging period in milliseconds, recommended value 15000-60000.
#define CPM_LOG_PERIOD_FAST 15000
#define CPM_LOG_PERIOD_MEDIUM 30000
#define CPM_LOG_PERIOD_SLOW 60000
#define CPM_LOG_PERIOD_VERY_SLOW 120000

// CPM period =  per minute (60 seconds)
#define CPM_PERIOD 60000

// Number of impulses
volatile unsigned long impulses;
// Last calculation
unsigned long last_time_cpm_calculated;
// CPM calculation interval
unsigned long log_period = CPM_LOG_PERIOD_FAST;
// CPM
unsigned long cpm;
// CPM history
std::vector<unsigned long> history;

// Display settings 128x64 OLED
#define MAX_ROW 128
#define MAX_LINE 64

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
  display_info
};

display_mode_t display_mode = display_cpm;
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
  case CPM_LOG_PERIOD_FAST:
    log_period = CPM_LOG_PERIOD_MEDIUM;
    text = "MEDIUM";
    break;

  case CPM_LOG_PERIOD_MEDIUM:
    log_period = CPM_LOG_PERIOD_SLOW;
    text = "SLOW";
    break;

  case CPM_LOG_PERIOD_SLOW:
    log_period = CPM_LOG_PERIOD_VERY_SLOW;
    text = "VERY SLOW";
    break;

  case CPM_LOG_PERIOD_VERY_SLOW:
    log_period = CPM_LOG_PERIOD_FAST;
    text = "FAST";
    break;
  }

  display.clear();
  display.drawString(0, 0, "Interval: " + text);
  display.drawString(0, 20, String(log_period / 1000) + " seconds");
  display.display();

  last_time_cpm_calculated = millis();
  impulses = 0;
  history.clear();
}

void setup()
{
  // put your setup code here, to run once:

  // Start Serial
  Serial.begin(115200);

  // Start Display
  display.init();
  display.flipScreenVertically();

  // Show start screen
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "Geiger");
  display.drawString(0, 16, "Display");
  display.drawXbm(128 - 32, 0, 32, 32, radiation_icon);

  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 40, "Copyright (c) 2021");
  display.drawString(0, 52, "Rene Zeldenthuis");
  display.display();

  // Link the onButtonClick function to be called on a click event.
  button.attachClick(onButtonClick);
  button.attachDoubleClick(onButtonDoubleClick);

  // Define external interrupts
  pinMode(GPIO_IN_GEIGER, INPUT);
  attachInterrupt(digitalPinToInterrupt(GPIO_IN_GEIGER), tube_impulse, FALLING);
}

void display_graph(unsigned long max_cpm)
{
  if (history.size())
  {
    // Display a history graph. First line reserved for text
    const float max_height = (MAX_LINE - 10) - 1;
    const auto cpm_adjust = max_cpm ? max_height / max_cpm : 0.0f;
    int16_t x = 0;
    for (auto it : history)
    {
      // Draw from the bottom up
      display.drawLine(x, MAX_LINE, x, MAX_LINE - it * cpm_adjust);
      x++;
    }
  }
}

void loop()
{
  // put your main code here, to run repeatedly:

  // keep watching the push button
  button.tick();

  // Calculate the CPM
  auto now = millis();
  if (now - last_time_cpm_calculated > log_period)
  {
    last_time_cpm_calculated = now;
    cpm = impulses * CPM_PERIOD / log_period;
    impulses = 0;

    // Update history
    if (history.size() >= MAX_ROW)
      history.pop_back();

    history.insert(history.begin(), cpm);

    Serial.println(cpm);

    redraw = true;
  }

  if (redraw)
  {
    display.clear();
    auto avg_cpm = std::accumulate(history.begin(), history.end(), 0.0f) / history.size();
    auto max_cpm = *std::max_element(history.begin(), history.end());

    static const String cpm_text("CPM");
    static const String ush_text("uS/h");
    static const String max_text("Max");
    static const String avg_text("Avg");

    switch (display_mode)
    {
    case display_cpm_graph_max:
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 0, cpm_text + ": " + String(cpm) + " " + max_text + ": " + String(max_cpm));
      display_graph(max_cpm);
      break;

    case display_ush_graph_max:
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 0, ush_text + ": " + String(cpm * CPM_USH_CONVERSION) + " " + max_text + ": " + String(max_cpm * CPM_USH_CONVERSION));
      display_graph(max_cpm);
      break;

    case display_cpm_graph_avg:
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 0, cpm_text + ": " + String(cpm) + " " + avg_text + ": " + String(avg_cpm));
      display_graph(max_cpm);
      break;

    case display_ush_graph_avg:
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 0, ush_text + ": " + String(cpm * CPM_USH_CONVERSION) + " " + avg_text + ": " + String(avg_cpm * CPM_USH_CONVERSION));
      display_graph(max_cpm);
      break;

    case display_cpm:
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 0, cpm_text + ":");
      display.setFont(ArialMT_Plain_24);
      display.drawString(0, 24, String(cpm));
      break;

    case display_ush:
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 0, ush_text + ":");
      display.setFont(ArialMT_Plain_24);
      display.drawString(0, 24, String(cpm * CPM_USH_CONVERSION));
      break;

    case display_cpm_avg:
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 0, cpm_text + " " + avg_text + ":");
      display.setFont(ArialMT_Plain_24);
      display.drawString(0, 24, String(avg_cpm));
      break;

    case display_ush_avg:
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 0, ush_text + " " + avg_text + ":");
      display.setFont(ArialMT_Plain_24);
      display.drawString(0, 24, String(avg_cpm * CPM_USH_CONVERSION));
      break;

    case display_info:
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 0, "Tube: " + tube_text);
      display.drawXbm(48, 20, 32, 32, radiation_icon);
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 54, "CPM => uS/h: " + String(CPM_USH_CONVERSION, 6));
      break;
    }

    display.display();
  }
}