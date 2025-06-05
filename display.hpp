#pragma once

#include <cstdio>
#include <cmath>
// Pimoroni Libraries for Pico Display Pack 2.0
#include "button.hpp"
#include "drivers/st7789/st7789.hpp"
#include "libraries/pico_display_2/pico_display_2.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "rgbled.hpp"
#include "pico/stdlib.h"

using namespace pimoroni;

// Stock data structure
struct StockData {
    char symbol[8];
    char duration[8];
    float current_price;
    float price_change;
    float percent_change;
    float history[100];
    int history_len;
};

// Display initialization and control functions
void initialize_display();
void update_display(const StockData& data);
void set_backlight(uint8_t brightness);

// Data management functions
void initialize_stock_data(StockData& data);
void update_stock_data(StockData& data, const char* symbol, float current_price, float price_change, float percent_change);

// Internal helper functions
namespace display_internal {
    void draw_header(const StockData& data);
    void draw_footer(const StockData& data);
    void draw_graph_and_labels(const StockData& data);
    float map_value(float value, float from_low, float from_high, float to_low, float to_high);
    float get_nice_step(float range);
}

// Button and LED access
extern Button button_a;
extern Button button_b;
extern Button button_x;
extern Button button_y;
extern RGBLED led; 