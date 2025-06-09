#include "pico-stock-ticker.hpp"
#include "ArduinoJson/Strings/JsonString.hpp"
#include "display.hpp"
#include "hardware/rtc.h"
#include "pico/util/datetime.h"
#include "tls_client.h"
#include <ArduinoJson.h>
#include <cstdint>
#include <ctime>
#include <vector>
#include <cstdio>  // for printf
#include <cstring> // for strcmp

static const uint8_t cert_ok[] = ROOT_CERT;

static SemaphoreHandle_t http_request_complete_sem = NULL;
static SemaphoreHandle_t wifi_connected_sem = NULL; // To signal HTTP task

volatile uint32_t ulIdleCycleCount = 0UL;

// Define the number of tasks we're tracking
const size_t NUM_TASKS = 4;  // main, blink, wifi, tls_client

// Initialize the stack usage tracking array
TaskStackUsage task_stack_usage[NUM_TASKS] = {
    {"MainThread", MAIN_TASK_STACK_SIZE, 0},
    {"BlinkThread", BLINK_TASK_STACK_SIZE, 0},
    {"WiFiThread", WIFI_TASK_STACK_SIZE, 0},
    {"TLSClientThread", HTTP_GET_TASK_STACK_SIZE, 0}
};

// Function to update stack usage for a task
static void update_task_stack_usage(const char* task_name) {
    TaskHandle_t task = xTaskGetHandle(task_name);
    if (task != NULL) {
        for (size_t i = 0; i < NUM_TASKS; i++) {
            if (strcmp(task_stack_usage[i].task_name, task_name) == 0) {
                task_stack_usage[i].high_water_mark = uxTaskGetStackHighWaterMark(task);
                break;
            }
        }
    }
}

// Function to print stack usage for all tasks
void print_task_stack_usage() {
    printf("\nTask Stack Usage Report:\n");
    printf("----------------------\n");
    for (size_t i = 0; i < NUM_TASKS; i++) {
        UBaseType_t stack_size_bytes = task_stack_usage[i].stack_size * sizeof(StackType_t);
        UBaseType_t high_water_bytes = task_stack_usage[i].high_water_mark * sizeof(StackType_t);
        UBaseType_t used_bytes = stack_size_bytes - high_water_bytes;
        float usage_percent = (float)used_bytes / stack_size_bytes * 100.0f;
        
        printf("%s:\n", task_stack_usage[i].task_name);
        printf("  Stack Size: %d bytes\n", stack_size_bytes);
        printf("  High Water Mark: %d bytes\n", high_water_bytes);
        printf("  Used: %d bytes (%.1f%%)\n", used_bytes, usage_percent);
        printf("  Free: %d bytes\n", high_water_bytes);
        printf("----------------------\n");
    }
}

void vApplicationIdleHook(void) {
	ulIdleCycleCount++;

	/* Example: Enter a low-power sleep mode.
	 * The specifics of HAL_PWR_EnterSLEEPMode are MCU-dependent.
	 * Ensure interrupts can wake the MCU.
	 */
	// HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
	(void)pcTaskName;
	(void)xTask;

	taskDISABLE_INTERRUPTS();
	printf("Stack overflow in task: %s\n", pcTaskName);
	for (;;)
		;
}

void vApplicationMallocFailedHook(void) {
	taskDISABLE_INTERRUPTS();
	printf("Malloc failed!\n");
	for (;;)
		;
}

// Turn led on or off
static void pico_set_led(bool led_on) {
	const char *str = led_on ? "on" : "off";
	printf("LED %s\n", str);
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
}

void blink_task(__unused void *params) {
	bool on = false;
	printf("blink_task starts\n");
	while (true) {
		static int last_core_id = -1;
		if (portGET_CORE_ID() != last_core_id) {
			last_core_id = portGET_CORE_ID();
			printf("blink task is on core %d\n", last_core_id);
		}
		update_task_stack_usage("BlinkThread");
		pico_set_led(on);
		on = !on;
		sleep_ms(LED_DELAY); // TODO: vary the LED with WiFi Connection
	}
}

void wifi_task(__unused void *params) {
	printf("wifi_task starts\n");

	// Enable wifi station
	cyw43_arch_enable_sta_mode();

	bool notified_http_task =
	    false; // Ensure we only signal once per connection

	while (true) {
		static int last_core_id = -1;

		if (portGET_CORE_ID() != last_core_id) {
			last_core_id = portGET_CORE_ID();
			printf("wifi task is on core %d\n", last_core_id);
		}

		int status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
		switch (status) {
		case CYW43_LINK_JOIN:
			printf("WiFi is connected\n");
			if (!notified_http_task && wifi_connected_sem != NULL) {
				printf("wifi_task: Signaling http_get_task that Wi-Fi is "
				       "connected.\n");
				xSemaphoreGive(wifi_connected_sem);
				notified_http_task = true;
			}
			break;
		case CYW43_LINK_DOWN:
			printf("WiFi disconnected\n");
			notified_http_task = false; // Allow re-notification on next connect
			break;
		case CYW43_LINK_FAIL:
			printf("Connection failed\n");
			notified_http_task = false;
			break;
		case CYW43_LINK_NONET:
			printf("WiFi not found\n");
			break;
		case CYW43_LINK_BADAUTH:
			printf("WiFi authentication failed\n");
			break;
		default:
			printf("WiFi link status is unknown\n");
		}

		printf("status: %d\n", status);

		if (status == CYW43_LINK_FAIL || status == CYW43_LINK_DOWN ||
		    status == CYW43_LINK_NONET) {
			printf("Retrying...\n");
			cyw43_arch_wifi_connect_timeout_ms(
			    ssid, password, CYW43_AUTH_WPA2_AES_PSK, timeout);
		} else if (status == CYW43_LINK_JOIN) {
			printf("Connected.\n");
			// Read the ip address in a human readable way
			static uint8_t *ip_address =
			    (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
			printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1],
			       ip_address[2], ip_address[3]);
		} else {
			break; // * Error Unrecoverable. Kill the task
		}

		vTaskDelay(pdMS_TO_TICKS(status == CYW43_LINK_JOIN ? 10000 : 1000));
		update_task_stack_usage("WiFiThread");
	}

	vTaskDelete(NULL);
}

// Buffer for MessagePack encoded messages
static char message_buffer[1024]; // Larger buffer to hold all messages
static uint8_t response_buffer[1024];

// Function to generate MessagePack encoded authentication request
static size_t generate_auth_request(char *buffer, size_t buffer_size) {
	JsonDocument doc;
	JsonObject obj = doc.to<JsonObject>();
	obj["token"] = TLS_CLIENT_AUTH_TOKEN;

	// Serialize to MessagePack format
	return serializeMsgPack(doc, buffer, buffer_size);
}

// Function to generate MessagePack encoded command request
static size_t generate_command_request(char *buffer, size_t buffer_size,
                                       const char *command,
                                       JsonVariant payload = JsonVariant()) {
	JsonDocument doc;
	JsonObject obj = doc.to<JsonObject>();
	obj["command"] = command;
	if (!payload.isNull()) {
		obj["payload"] = payload;
	}

	// Serialize to MessagePack format
	return serializeMsgPack(doc, buffer, buffer_size);
}

// Generic function to send a command and receive response
static CommandError send_command(TLS_CLIENT_HANDLE handle, const char *command,
                                 JsonVariant payload, uint8_t *recv_buffer,
                                 size_t recv_buffer_size,
                                 JsonDocument &response_doc,
                                 char *message_buffer,
                                 size_t message_buffer_size) {
	// Generate and send command request
	size_t cmd_len = generate_command_request(
	    message_buffer, message_buffer_size, command, payload);
	message_buffer[cmd_len] = '\0';

	printf("Sending command '%s' (%d bytes)\n", command, cmd_len);

	int recv_len =
	    tls_client_send_and_recv(handle, (uint8_t *)message_buffer, cmd_len,
	                             recv_buffer, recv_buffer_size,
	                             5000 // 5 second timeout per command
	    );

	if (recv_len <= 0) {
		printf("Error receiving command response: %d\n", recv_len);
		return CMD_RECV_ERROR;
	}

	recv_buffer[recv_len] = '\0';

	// Deserialize the MessagePack response
	DeserializationError error =
	    deserializeMsgPack(response_doc, recv_buffer, recv_len);
	message_buffer[0] = '\0';

	if (error) {
		printf("MessagePack deserialization failed: %s\n", error.c_str());
		return CMD_DESERIALIZE_ERROR;
	}

#ifdef DEBUG
	printf("Deserialized response:\n");
	serializeJsonPretty(response_doc, message_buffer, message_buffer_size);
	printf("%s\n", message_buffer);
#endif

	return CMD_SUCCESS;
}

// Function to parse server time string and set RTC time
static bool parse_and_set_rtc_time(const char *time_str) {
	// Expected format: "YYYY-MM-DD HH:MM:SS TZ"
	int year, month, day, hour, min, sec;
	int dotw = 1;
	char tz[4];

	// Parse the time string
	if (sscanf(time_str, "%d-%d-%d %d:%d:%d %3s", &year, &month, &day, &hour,
	           &min, &sec, tz) != 7) {
		printf("Failed to parse time string: %s\n", time_str);
		return false;
	}

	// Create datetime_t struct
	datetime_t t = {
	    .year = (int16_t)year,
	    .month = (int8_t)month,
	    .day = (int8_t)day,
	    .dotw = (int8_t)dotw,
	    .hour = (int8_t)hour,
	    .min = (int8_t)min,
	    .sec = (int8_t)sec
	};

	// Set the RTC time
	if (!rtc_set_datetime(&t)) {
		printf("Failed to set RTC time\n");
		return false;
	}

	printf("Successfully set RTC time to: %04d-%02d-%02d %02d:%02d:%02d %s\n",
	       t.year, t.month, t.day, t.hour, t.min, t.sec, tz);
	return true;
}

// Function to parse stock data from response
static bool parse_stock_data(JsonDocument &response_doc,
                             StockData &stock_data) {
	if (!response_doc["stock_data"].is<JsonObject>()) {
		printf("No stock data in response\n");
		return false;
	}

	const JsonObject stock_data_obj = response_doc["stock_data"];
	const JsonArray data_array = stock_data_obj["data"];

	// Get the ticker symbol
	const char *ticker = stock_data_obj["ticker"];
	strncpy(stock_data.symbol, ticker, sizeof(stock_data.symbol) - 1);
	stock_data.symbol[sizeof(stock_data.symbol) - 1] = '\0';

	// Get the duration
	const char *duration = stock_data_obj["duration"];
	strncpy(stock_data.duration, duration, sizeof(stock_data.duration) - 1);
	stock_data.duration[sizeof(stock_data.duration) - 1] = '\0';

	// Clear existing history
	stock_data.history_len = 0;

	// Process each data point
	for (const JsonObject &data_point : data_array) {
		if (stock_data.history_len >= 30) { // Maximum history size
			break;
		}

		OHLC &ohlc = stock_data.history[stock_data.history_len];
		ohlc.open = data_point["Open"];
		ohlc.high = data_point["High"];
		ohlc.low = data_point["Low"];
		ohlc.close = data_point["Close"];

		// Store timestamp
		const char *timestamp_str = data_point["Date"];
		strncpy(stock_data.timestamp, timestamp_str,
		        sizeof(stock_data.timestamp) - 1);
		stock_data.timestamp[sizeof(stock_data.timestamp) - 1] = '\0';

		// Update current price and other metrics
		if (stock_data.history_len == 0) {
			stock_data.open_price = ohlc.open;
			stock_data.high_price = ohlc.high;
			stock_data.low_price = ohlc.low;
		} else {
			stock_data.high_price = std::max(stock_data.high_price, ohlc.high);
			stock_data.low_price = std::min(stock_data.low_price, ohlc.low);
		}

		stock_data.current_price = ohlc.close;
		stock_data.history_len++;
	}

	// Calculate price changes
	if (stock_data.history_len > 0) {
		stock_data.price_change =
		    stock_data.current_price - stock_data.open_price;
		stock_data.percent_change =
		    (stock_data.price_change / stock_data.open_price) * 100.0f;
	}

	return stock_data.history_len > 0;
}

void tls_client_task(__unused void *params) {
	printf("tls_client_task starts\n");
	xSemaphoreTake(wifi_connected_sem, portMAX_DELAY);
	printf("WiFi connected, starting TLS client test\n");

	while (true) {
		static int last_core_id = -1;
		if (portGET_CORE_ID() != last_core_id) {
			last_core_id = portGET_CORE_ID();
			printf("tls client task is on core %d\n", last_core_id);
		}
		update_task_stack_usage("TLSClientThread");
		// Add stack usage report after each connection cycle
		print_task_stack_usage();

		// Initialize and connect to the server
		TLS_CLIENT_HANDLE handle = tls_client_init_and_connect(
		    TLS_CLIENT_SERVER, TLS_CLIENT_PORT, cert_ok, sizeof(cert_ok));

		if (!handle) {
			printf("Failed to connect to TLS server\n");
			vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retry
			continue;
		}

		// First send authentication request
		size_t auth_len =
		    generate_auth_request(message_buffer, sizeof(message_buffer));
		message_buffer[auth_len] = '\0';

		printf("Sending auth request (%d bytes)\n", auth_len);

		// Send auth request and receive response
		int recv_len = tls_client_send_and_recv(
		    handle, (uint8_t *)message_buffer, auth_len, response_buffer,
		    sizeof(response_buffer),
		    5000 // 5 second timeout for auth
		);

		if (recv_len <= 0) {
			printf("Error receiving auth response: %d\n", recv_len);
			tls_client_close(handle);
			vTaskDelay(pdMS_TO_TICKS(5000));
			continue;
		}

		response_buffer[recv_len] = '\0';

		// Deserialize the MessagePack response
		JsonDocument response_doc;
		DeserializationError error =
		    deserializeMsgPack(response_doc, response_buffer, recv_len);
		message_buffer[0] = '\0';

		if (error) {
			printf("MessagePack deserialization failed: %s\n", error.c_str());
		} else {
			printf("Deserialized response:\n");
			serializeJsonPretty(response_doc, message_buffer,
			                    sizeof(message_buffer));
			printf("%s\n", message_buffer);
		}
		response_doc.clear();

		// Prepare array of commands to send
		struct Command {
			const char *name;
			JsonVariant payload;
		};

		Command commands[] = {{"ping", JsonVariant()},
		                      {"get_time", JsonVariant()}};

		// Send each command one by one
		for (const auto &cmd : commands) {
			CommandError err =
			    send_command(handle, cmd.name, cmd.payload, response_buffer,
			                 sizeof(response_buffer), response_doc,
			                 message_buffer, sizeof(message_buffer));

			if (err != CMD_SUCCESS) {
				printf("Command '%s' failed with error %d\n", cmd.name, err);
				break;
			}

			// Handle get_time command response
			if (strcmp(cmd.name, "get_time") == 0) {
				const char *server_time = response_doc["server_time"];
				if (server_time) {
					if (!parse_and_set_rtc_time(server_time)) {
						printf("Failed to set RTC time from server response\n");
					}
				} else {
					printf("No server_time in response\n");
				}
			}

			// Handle get_stock_data command response
			if (strcmp(cmd.name, "get_stock_data") == 0) {
			}

			response_doc.clear();
			// Small delay between commands
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		// Send the get_stock_data command separately
		JsonDocument doc;
		doc["command"] = "get_stock_data";
		doc["ticker"] = "AAPL";
		doc["duration"] = "1d";
		doc["interval"] = "1h";

		CommandError err =
		    send_command(handle, "get_stock_data", doc, response_buffer,
		                 sizeof(response_buffer), response_doc, message_buffer,
		                 sizeof(message_buffer));

		if (err != CMD_SUCCESS) {
			printf("Command 'get_stock_data' failed with error %d\n", err);
		}
		extern StockData stock_data;

		if (parse_stock_data(response_doc, stock_data)) {
			printf("Received %d data points for %s\n", stock_data.history_len,
			       stock_data.symbol);

			// Process the stock data as needed
			printf("Current Price: %.2f, Change: %.2f (%.2f%%)\n",
			       stock_data.current_price, stock_data.price_change,
			       stock_data.percent_change);

			// Update the display with the new data
			update_display(stock_data);
		} else {
			printf("Failed to parse stock data\n");
		}
		response_doc.clear();

		// Close the connection
		tls_client_close(handle);

		// Wait before next attempt
		vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second delay between attempts
	}
	vTaskDelete(NULL);
}

void main_task(__unused void *params) {
	rtc_init();

	// Initialise the Wi-Fi chip
	if (cyw43_arch_init()) {
		printf("Wi-Fi init failed\n");
		return;
	}

	// Create semaphores before starting tasks that use them
	http_request_complete_sem = xSemaphoreCreateBinary();
	if (http_request_complete_sem == NULL) {
		printf("Failed to create http_request_complete_sem\n");
	}

	wifi_connected_sem = xSemaphoreCreateBinary();
	if (wifi_connected_sem == NULL) {
		printf("Failed to create wifi_connected_sem\n");
	}

	// Initialize display
	initialize_display();

	// Create stock data structure
	extern StockData stock_data;
	initialize_stock_data(stock_data);

	// start the led blinking
	xTaskCreate(blink_task, "BlinkThread", BLINK_TASK_STACK_SIZE, NULL,
	            BLINK_TASK_PRIORITY, NULL);
	xTaskCreate(wifi_task, "WiFiThread", WIFI_TASK_STACK_SIZE, NULL,
	            WIFI_TASK_PRIORITY, NULL);
	xTaskCreate(tls_client_task, "TLSClientThread", HTTP_GET_TASK_STACK_SIZE,
	            NULL, HTTP_GET_TASK_PRIORITY, NULL);

	while (true) {
		static int last_core_id = -1;
		if (portGET_CORE_ID() != last_core_id) {
			last_core_id = portGET_CORE_ID();
			printf("main task is on core %d\n", last_core_id);
		}

		update_task_stack_usage("MainThread");
		// Update the display with current stock data
		update_display(stock_data);

		// Handle button inputs
		if (button_a.raw()) {
			// TODO: Implement button A functionality
		}
		if (button_b.raw()) {
			// TODO: Implement button B functionality
		}
		if (button_x.raw()) {
			// TODO: Implement button X functionality
		}
		if (button_y.raw()) {
			// TODO: Implement button Y functionality
		}

		vTaskDelay(10);
	}

	cyw43_arch_deinit();
	vTaskDelete(NULL);
}

void vLaunch(void) {
	TaskHandle_t task;
	xTaskCreate(main_task, "MainThread", MAIN_TASK_STACK_SIZE, NULL,
	            MAIN_TASK_PRIORITY, &task);

	/* Start the tasks and timer running. */
	vTaskStartScheduler();
}

int main(void) {
	stdio_init_all();

	/* Configure the hardware ready to run the demo. */
	const char *rtos_name;
	rtos_name = "FreeRTOS SMP";

	printf("Starting %s on both cores:\n", rtos_name);
	vLaunch();
	return 0;
}
