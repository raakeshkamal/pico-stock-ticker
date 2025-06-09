#include "display.hpp"
#include "hardware/rtc.h"
#include "pico/rand.h"
#include "pico/util/datetime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <numeric>
#include <string>
#include <vector>

// Pico SDK
#include "pico/stdlib.h"

using namespace pimoroni;

// Initialize the display driver (320x240)
static ST7789 st7789(PicoDisplay2::WIDTH, PicoDisplay2::HEIGHT, ROTATE_180,
                     false, get_spi_pins(BG_SPI_FRONT));

// Initialize the graphics library (using RGB332 for better memory efficiency)
static PicoGraphics_PenRGB332 graphics(st7789.width, st7789.height, nullptr);

// Initialize LED and buttons
RGBLED led(PicoDisplay2::LED_R, PicoDisplay2::LED_G, PicoDisplay2::LED_B);
Button button_a(PicoDisplay2::A);
Button button_b(PicoDisplay2::B);
Button button_x(PicoDisplay2::X);
Button button_y(PicoDisplay2::Y);

// --- UI Layout Configuration ---
static const int GRAPH_TOP = 20;
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
void draw_header(const StockData &data);
void draw_footer(const StockData &data);
void draw_graph_and_labels(const StockData &data);
float map_value(float value, float from_low, float from_high, float to_low,
                float to_high);
float get_nice_step(float range);
} // namespace display_internal

// --- Function Prototypes ---
void initialize_display();
void set_backlight(uint8_t brightness);
void update_display(const StockData &data);
void initialize_stock_data(StockData &data);
void update_stock_data(StockData &data, const char *symbol, float current_price,
                       float price_change, float percent_change);

// Helper function to format RTC time to 12-hour format
void format_rtc_time_to_12h(const datetime_t &t, char *output,
                            size_t output_size) {
	// Convert to 12-hour format
	int hour_12 = t.hour % 12;
	if (hour_12 == 0)
		hour_12 = 12; // Convert 0 to 12 for 12 AM
	const char *ampm = (t.hour >= 12) ? "PM" : "AM";
	snprintf(output, output_size, "%02d:%02d %s", hour_12, t.min, ampm);
}

void initialize_display() {
	st7789.set_backlight(200); // Set brightness (0-255)

	// Create pens from RGB values to match the image
	BG_DARK_BLUE = graphics.create_pen(4, 22, 48);
	TEXT_WHITE = graphics.create_pen(220, 220, 230);
	TEXT_GREEN = graphics.create_pen(0, 200, 80);
	LINE_WHITE = graphics.create_pen(220, 220, 230);
	FOOTER_BG = graphics.create_pen(10, 36, 70);
}

void set_backlight(uint8_t brightness) { st7789.set_backlight(brightness); }

void update_display(StockData &data) {
	// Clear screen with the main background color
	graphics.set_pen(BG_DARK_BLUE);
	graphics.clear();

	// Get current time from RTC
	datetime_t now;
	rtc_get_datetime(&now);
	char timestamp[50];
	format_rtc_time_to_12h(now, timestamp, sizeof(timestamp));
	strcpy(data.timestamp, timestamp);

	// Draw all UI components
	display_internal::draw_header(data);
	display_internal::draw_graph_and_labels(data);
	display_internal::draw_footer(data);

	// Push the completed frame to the screen
	st7789.update(&graphics);
}

void initialize_stock_data(StockData &data) {
	snprintf(data.symbol, sizeof(data.symbol), "NVDA");
	snprintf(data.duration, sizeof(data.duration), "1h");
	data.current_price = 878.37f;

	datetime_t t;
	rtc_get_datetime(&t);

	char timestamp[50];
	format_rtc_time_to_12h(t, timestamp, sizeof(timestamp));
	strcpy(data.timestamp, timestamp);

	// Generate random price history data
	data.history_len = 30;

	// Create a more sophisticated random seed using multiple sources
	uint32_t seed = t.sec + t.min + t.hour + t.day + t.month + t.year;
	seed +=
	    (uint32_t)get_rand_32(); // Use Pico's hardware random number generator

	srand(seed); // Seed random number generator with combined entropy
	float base_price =
	    850.0f + (rand() % 100); // Random base price between 850-950
	float trend = (rand() % 2) ? 1.0f : -1.0f; // Random up or down trend

	// Generate first period's data
	float current_price = base_price;
	float volatility = (rand() % 5) + 1; // Random volatility between 1-5
	data.history[0].open = current_price;
	data.history[0].high = current_price + (rand() % (int)volatility);
	data.history[0].low = current_price - (rand() % (int)volatility);
	data.history[0].close =
	    current_price + ((rand() % 10) - 5); // Random close price near the open

	// Generate subsequent periods
	for (int i = 1; i < 30; ++i) {
		// Add some random noise and trend
		float noise = (rand() % 10) - 5; // Random noise between -5 and +5
		float trend_factor =
		    trend * (i / 10.0f); // Gradually increasing trend effect

		// Use previous close as current open
		current_price = data.history[i - 1].close;
		volatility = (rand() % 5) + 1; // Random volatility between 1-5

		data.history[i].open = current_price;
		data.history[i].high = current_price + (rand() % (int)volatility);
		data.history[i].low = current_price - (rand() % (int)volatility);
		data.history[i].close =
		    current_price +
		    ((rand() % 10) - 5); // Random close price near the open
	}

	// Set initial values from first OHLC data point
	data.open_price = data.history[0].open;
	data.high_price = data.history[0].high;
	data.low_price = data.history[0].low;

	// Find high and low prices from history
	for (int i = 1; i < data.history_len; i++) {
		if (data.history[i].high > data.high_price) {
			data.high_price = data.history[i].high;
		}
		if (data.history[i].low < data.low_price) {
			data.low_price = data.history[i].low;
		}
	}

	// Calculate price change and percent change using the last close price
	data.price_change =
	    data.current_price - data.history[data.history_len - 1].close;
	data.percent_change =
	    (data.price_change / data.history[data.history_len - 1].close) * 100.0f;
}

void update_stock_data(StockData &data, const char *symbol, float current_price,
                       float price_change, float percent_change) {
	snprintf(data.symbol, sizeof(data.symbol), "%s", symbol);
	data.current_price = current_price;
	data.price_change = price_change;
	data.percent_change = percent_change;
}

namespace display_internal {
void draw_header(const StockData &data) {
	graphics.set_pen(TEXT_WHITE);

	// Convert and display timestamp in 12-hour format
	graphics.text(data.timestamp, Point(5, 10), 200, 2);

	// Calculate exact text width using the graphics library
	int symbol_width = graphics.measure_text(data.symbol, 3.0f);
	int center_x = (PicoDisplay2::WIDTH - symbol_width) / 2;
	graphics.text(data.symbol, Point(center_x, 10), 200, 3);

	// Set color based on positive or negative change
	graphics.set_pen(data.price_change >= 0 ? TEXT_GREEN : TEXT_WHITE);

	char buffer[16];
	snprintf(buffer, 16, "%+.2f", data.price_change);
	// Calculate exact text width using the graphics library
	int price_width = graphics.measure_text(buffer, 2.0f);
	int right_x =
	    PicoDisplay2::WIDTH - price_width - 10; // 10px padding from right edge
	graphics.text(buffer, Point(right_x, 10), 100, 2);
}

void draw_footer(const StockData &data) {
	// Draw footer background rectangle
	graphics.set_pen(FOOTER_BG);
	graphics.rectangle(Rect(0, GRAPH_BOTTOM, PicoDisplay2::WIDTH,
	                        PicoDisplay2::HEIGHT - GRAPH_BOTTOM));

	// Draw text
	graphics.set_pen(TEXT_WHITE);

	// Calculate widths of all text elements
	char buffer[16];
	snprintf(buffer, 16, "H:%.2f", data.high_price);
	int high_width = graphics.measure_text(buffer, 2.0f);

	snprintf(buffer, 16, "L:%.2f", data.low_price);
	int low_width = graphics.measure_text(buffer, 2.0f);

	int duration_width = graphics.measure_text(data.duration, 2.0f);

	// Format percentage with a comma separator
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
	int percent_width = graphics.measure_text(percent_str, 2.0f);

	// Draw all elements left-justified with no padding
	int x = 5; // Start from left edge

	// Draw duration
	graphics.text(data.duration, Point(x, GRAPH_BOTTOM + 8), 100, 2);

	// Draw high price
	int high_price_x = PicoDisplay2::WIDTH / 3.0f;
	x = high_price_x - (high_width / 2);
	snprintf(buffer, 16, "H:%.2f", data.high_price);
	graphics.text(buffer, Point(x, GRAPH_BOTTOM + 8), 100, 2);

	// Draw low price
	int low_price_x = (PicoDisplay2::WIDTH / 3.0f) * 2;
	x = low_price_x - (low_width / 2);
	snprintf(buffer, 16, "L:%.2f", data.low_price);
	graphics.text(buffer, Point(x, GRAPH_BOTTOM + 8), 100, 2);

	// Set color for arrow and percentage
	graphics.set_pen(data.percent_change >= 0 ? TEXT_GREEN : TEXT_WHITE);

	// Calculate total width of percentage and arrow
	int arrow_width = 8; // Width of the triangle
	int total_width = percent_width + arrow_width;

	// Position from right edge
	int right_x = PicoDisplay2::WIDTH - total_width - 10;

	// Draw the up/down arrow (a filled triangle)
	if (data.percent_change >= 0) {
		graphics.triangle(Point(right_x, GRAPH_BOTTOM + 10),
		                  Point(right_x + 8, GRAPH_BOTTOM + 10),
		                  Point(right_x + 4, GRAPH_BOTTOM + 18));
	} else {
		graphics.triangle(Point(right_x, GRAPH_BOTTOM + 10),
		                  Point(right_x + 8, GRAPH_BOTTOM + 10),
		                  Point(right_x + 4, GRAPH_BOTTOM + 18));
	}

	// Draw percentage right after the arrow
	graphics.text(percent_str,
	              Point(right_x + arrow_width + 4, GRAPH_BOTTOM + 8), 100, 2);
}

void draw_graph_and_labels(const StockData &data) {
	if (data.history_len == 0)
		return;

	float min_price = data.history[0].low;
	float max_price = data.history[0].high;
	for (int i = 1; i < data.history_len; ++i) {
		if (data.history[i].low < min_price)
			min_price = data.history[i].low;
		if (data.history[i].high > max_price)
			max_price = data.history[i].high;
	}
	float price_range = max_price - min_price;

	// Add padding to the Y-axis so the graph doesn't touch the edges
	min_price -= price_range * 0.1f;
	max_price += price_range * 0.1f;
	price_range = max_price - min_price;
	if (price_range == 0)
		price_range = 1; // Avoid division by zero

	// Draw Y-Axis Labels
	graphics.set_pen(TEXT_WHITE);
	float step = get_nice_step(price_range);
	float first_label = floor(min_price / step) * step;

	for (float val = first_label; val <= max_price; val += step) {
		if (val < min_price)
			continue;
		int y = map_value(val, min_price, max_price, GRAPH_BOTTOM, GRAPH_TOP);

		// Skip labels that would overlap with the header area
		// Add 20 pixels buffer from the top to account for text height
		if (y < GRAPH_TOP + 20)
			continue;

		if (y < GRAPH_BOTTOM) {
			char label_str[10];
			snprintf(label_str, 10, "%d", (int)roundf(val));
			graphics.text(label_str, Point(Y_LABELS_X, y - 8), 50, 2);
		}
	}

	// Calculate candlestick width and spacing
	float raw_width =
	    (GRAPH_RIGHT - GRAPH_LEFT) / (float)data.history_len * 0.8f;
	int candle_width = (int)raw_width;
	if (candle_width % 2 == 0) {
		candle_width--; // Make it odd
	}
	float candle_spacing =
	    (GRAPH_RIGHT - GRAPH_LEFT) / (float)data.history_len * 0.2f;

	// Draw Candlesticks
	for (int i = 0; i < data.history_len; ++i) {
		float x =
		    map_value(i, 0, data.history_len - 1, GRAPH_LEFT, GRAPH_RIGHT);

		// Calculate y positions for OHLC
		int open_y = map_value(data.history[i].open, min_price, max_price,
		                       GRAPH_BOTTOM, GRAPH_TOP);
		int close_y = map_value(data.history[i].close, min_price, max_price,
		                        GRAPH_BOTTOM, GRAPH_TOP);
		int high_y = map_value(data.history[i].high, min_price, max_price,
		                       GRAPH_BOTTOM, GRAPH_TOP);
		int low_y = map_value(data.history[i].low, min_price, max_price,
		                      GRAPH_BOTTOM, GRAPH_TOP);

		// Draw the wick (high-low line)
		graphics.set_pen(LINE_WHITE);
		graphics.line(Point(x, high_y), Point(x, low_y));

		// Draw the body
		bool is_bullish = data.history[i].close >= data.history[i].open;
		graphics.set_pen(is_bullish ? TEXT_GREEN : TEXT_WHITE);

		// Draw filled rectangle for the body
		int body_top = std::min(open_y, close_y);
		int body_height = std::abs(close_y - open_y);
		if (body_height == 0)
			body_height = 1; // Ensure at least 1px height for doji

		graphics.rectangle(
		    Rect(x - candle_width / 2, body_top, candle_width, body_height));
	}
}

float map_value(float value, float from_low, float from_high, float to_low,
                float to_high) {
	return (value - from_low) * (to_high - to_low) / (from_high - from_low) +
	       to_low;
}

float get_nice_step(float range) {
	if (range == 0)
		return 1.0f;
	float exponent = floorf(log10f(range));
	float power_of_10 = powf(10, exponent);
	float rel_range = range / power_of_10;

	if (rel_range < 2.0f)
		return 0.2f * power_of_10;
	if (rel_range < 5.0f)
		return 0.5f * power_of_10;
	return 1.0f * power_of_10;
}
} // namespace display_internal