#include "display.hpp"
#include <numeric>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Pico SDK
#include "pico/stdlib.h"

using namespace pimoroni;

// Initialize the display driver (320x240)
static ST7789 st7789(
  PicoDisplay2::WIDTH,
  PicoDisplay2::HEIGHT,
  ROTATE_0,
  false,
  get_spi_pins(BG_SPI_FRONT)
);

// Initialize the graphics library (using RGB332 for better memory efficiency)
static PicoGraphics_PenRGB332 graphics(
  st7789.width,
  st7789.height,
  nullptr
);

// Initialize LED and buttons
RGBLED led(PicoDisplay2::LED_R, PicoDisplay2::LED_G, PicoDisplay2::LED_B);
Button button_a(PicoDisplay2::A);
Button button_b(PicoDisplay2::B);
Button button_x(PicoDisplay2::X);
Button button_y(PicoDisplay2::Y);

// --- UI Layout Configuration ---
static const int GRAPH_TOP = 50;
static const int GRAPH_BOTTOM = 210;
static const int GRAPH_LEFT = 10;
static const int GRAPH_RIGHT = 280;
static const int Y_LABELS_X = 285;

// --- Color Palette ---
static Pen BG_DARK_BLUE;
static Pen TEXT_WHITE;
static Pen TEXT_GREEN;
static Pen LINE_WHITE;
static Pen FOOTER_BG;

// --- Stock Data ---
static StockData nvda_data;

// Forward declarations of internal functions
namespace display_internal {
    void draw_header(const StockData& data);
    void draw_footer(const StockData& data);
    void draw_graph_and_labels(const StockData& data);
    float map_value(float value, float from_low, float from_high, float to_low, float to_high);
    float get_nice_step(float range);
}

// --- Function Prototypes ---
void initialize_display();
void set_backlight(uint8_t brightness);
void update_display(const StockData& data);
void initialize_stock_data(StockData& data);
void update_stock_data(StockData& data, const char* symbol, float current_price, float price_change, float percent_change);

void initialize_display() {
  st7789.set_backlight(200); // Set brightness (0-255)

  // Create pens from RGB values to match the image
  BG_DARK_BLUE = graphics.create_pen(4, 22, 48);
  TEXT_WHITE = graphics.create_pen(220, 220, 230);
  TEXT_GREEN = graphics.create_pen(0, 200, 80);
  LINE_WHITE = graphics.create_pen(220, 220, 230);
  FOOTER_BG = graphics.create_pen(10, 36, 70);
}

void set_backlight(uint8_t brightness) {
  st7789.set_backlight(brightness);
}

void update_display(const StockData& data) {
  // Clear screen with the main background color
  graphics.set_pen(BG_DARK_BLUE);
  graphics.clear();

  // Draw all UI components
  display_internal::draw_header(data);
  display_internal::draw_graph_and_labels(data);
  display_internal::draw_footer(data);

  // Push the completed frame to the screen
  st7789.update(&graphics);
}

void initialize_stock_data(StockData& data) {
  snprintf(data.symbol, sizeof(data.symbol), "NVDA");
  snprintf(data.duration, sizeof(data.duration), "1h");
  data.current_price = 878.37f;
  data.price_change = 11.43f;
  data.percent_change = 1.32f;

  // Generate fake data that looks like the chart in the image
  data.history_len = 30;
  float fake_history[30] = {
    868, 866, 869, 867, 865, 867, 864, 866, 863, 865, 862, 864, 861,
    863, 860, 862, 864, 862, 865, 867, 864, 868, 870, 868, 872, 875,
    873, 877, 879, 881};
  for (int i = 0; i < 30; ++i) {
    data.history[i] = fake_history[i];
  }
}

void update_stock_data(StockData& data, const char* symbol, float current_price, float price_change, float percent_change) {
  snprintf(data.symbol, sizeof(data.symbol), "%s", symbol);
  data.current_price = current_price;
  data.price_change = price_change;
  data.percent_change = percent_change;
}

namespace display_internal {
  void draw_header(const StockData& data) {
    graphics.set_pen(TEXT_WHITE);
    graphics.text(data.duration, Point(10, 10), 200, 3);
    graphics.text(data.symbol, Point(130, 10), 200, 3);

    // Set color based on positive or negative change
    graphics.set_pen(data.price_change >= 0 ? TEXT_GREEN : TEXT_WHITE);

    char buffer[16];
    snprintf(buffer, 16, "%+.2f", data.price_change);
    graphics.text(buffer, Point(220, 10), 100, 2);

    snprintf(buffer, 16, "%+.2f%%", data.percent_change);
    graphics.text(buffer, Point(220, 30), 100, 2);
  }

  void draw_footer(const StockData& data) {
    // Draw footer background rectangle
    graphics.set_pen(FOOTER_BG);
    graphics.rectangle(
      Rect(0, GRAPH_BOTTOM, PicoDisplay2::WIDTH, PicoDisplay2::HEIGHT - GRAPH_BOTTOM)
    );

    // Draw text
    graphics.set_pen(TEXT_WHITE);
    graphics.text(data.symbol, Point(10, GRAPH_BOTTOM + 8), 100, 2);

    char price_str[16];
    snprintf(price_str, 16, "%.2f", data.current_price);
    graphics.text(price_str, Point(80, GRAPH_BOTTOM + 8), 100, 2);

    // Set color for arrow and percentage
    graphics.set_pen(data.percent_change >= 0 ? TEXT_GREEN : TEXT_WHITE);

    // Draw the up/down arrow (a filled triangle)
    if (data.percent_change >= 0) {
      graphics.triangle(
        Point(170, GRAPH_BOTTOM + 18),
        Point(178, GRAPH_BOTTOM + 18),
        Point(174, GRAPH_BOTTOM + 10)
      );
    } else {
      graphics.triangle(
        Point(170, GRAPH_BOTTOM + 10),
        Point(178, GRAPH_BOTTOM + 10),
        Point(174, GRAPH_BOTTOM + 18)
      );
    }

    // Format percentage with a comma separator to match the image
    char percent_buf[10];
    snprintf(percent_buf, 10, "%.2f", data.percent_change);
    char percent_str[12];
    int len = 0;
    for (int i = 0; percent_buf[i] != '\0'; ++i) {
      if (percent_buf[i] == '.') {
        percent_str[len++] = ',';
      } else {
        percent_str[len++] = percent_buf[i];
      }
    }
    percent_str[len++] = '%';
    percent_str[len] = '\0';
    graphics.text(percent_str, Point(190, GRAPH_BOTTOM + 8), 100, 2);
  }

  void draw_graph_and_labels(const StockData& data) {
    if (data.history_len == 0) return;

    float min_price = data.history[0];
    float max_price = data.history[0];
    for (int i = 1; i < data.history_len; ++i) {
      if (data.history[i] < min_price) min_price = data.history[i];
      if (data.history[i] > max_price) max_price = data.history[i];
    }
    float price_range = max_price - min_price;

    // Add padding to the Y-axis so the graph doesn't touch the edges
    min_price -= price_range * 0.1f;
    max_price += price_range * 0.1f;
    price_range = max_price - min_price;
    if (price_range == 0) price_range = 1; // Avoid division by zero

    // Draw Y-Axis Labels
    graphics.set_pen(TEXT_WHITE);
    float step = get_nice_step(price_range);
    float first_label = floor(min_price / step) * step;

    for (float val = first_label; val <= max_price; val += step) {
      if (val < min_price) continue;
      int y = map_value(val, min_price, max_price, GRAPH_BOTTOM, GRAPH_TOP);
      if (y > GRAPH_TOP - 10 && y < GRAPH_BOTTOM) {
        char label_str[10];
        snprintf(label_str, 10, "%d", (int)roundf(val));
        graphics.text(label_str, Point(Y_LABELS_X, y - 8), 50, 2);
      }
    }

    // Draw Graph Line
    graphics.set_pen(LINE_WHITE);
    Point prev_point;
    for (int i = 0; i < data.history_len; ++i) {
      int x = map_value(i, 0, data.history_len - 1, GRAPH_LEFT, GRAPH_RIGHT);
      int y = map_value(
        data.history[i],
        min_price,
        max_price,
        GRAPH_BOTTOM,
        GRAPH_TOP
      );
      Point current_point(x, y);

      if (i > 0) {
        graphics.line(prev_point, current_point);
      }
      prev_point = current_point;
    }
  }

  float map_value(float value, float from_low, float from_high, float to_low, float to_high) {
    return (value - from_low) * (to_high - to_low) / (from_high - from_low) + to_low;
  }

  float get_nice_step(float range) {
    if (range == 0) return 1.0f;
    float exponent = floorf(log10f(range));
    float power_of_10 = powf(10, exponent);
    float rel_range = range / power_of_10;

    if (rel_range < 2.0f) return 0.2f * power_of_10;
    if (rel_range < 5.0f) return 0.5f * power_of_10;
    return 1.0f * power_of_10;
  }
}