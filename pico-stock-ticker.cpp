#include "pico-stock-ticker.hpp"
#include "tls_client.h"
#include "display.hpp"
#include <ArduinoJson.h>
#include <cstdint>
#include "hardware/rtc.h"
#include "pico/util/datetime.h"

static const uint8_t cert_ok[] = ROOT_CERT;

static SemaphoreHandle_t http_request_complete_sem = NULL;
static SemaphoreHandle_t wifi_connected_sem = NULL; // To signal HTTP task


volatile uint32_t ulIdleCycleCount = 0UL;

void vApplicationIdleHook( void )
{
    ulIdleCycleCount++;

    /* Example: Enter a low-power sleep mode.
     * The specifics of HAL_PWR_EnterSLEEPMode are MCU-dependent.
     * Ensure interrupts can wake the MCU.
     */
    // HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) xTask;

    taskDISABLE_INTERRUPTS();
    printf("Stack overflow in task: %s\n", pcTaskName);
    for( ;; );
}

void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();
    printf("Malloc failed!\n");
    for( ;; );
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
	}

	vTaskDelete(NULL);
}

#define TLS_CLIENT_SERVER        "192.168.0.41"  // Change this to your server's IP or hostname
#define TLS_CLIENT_PORT          8443         // Server listens on port 8443
#define TLS_CLIENT_AUTH_TOKEN    "supersecretclienttoken12345abcdef"  // Must match server's CLIENT_AUTH_TOKEN

// Buffer for MessagePack encoded messages
static char message_buffer[1024];  // Larger buffer to hold all messages
static uint8_t response_buffer[1024];

// Function to generate MessagePack encoded authentication request
static size_t generate_auth_request(char* buffer, size_t buffer_size) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["token"] = TLS_CLIENT_AUTH_TOKEN;
    
    // Serialize to MessagePack format
    return serializeMsgPack(doc, buffer, buffer_size);
}

// Function to generate MessagePack encoded command request
static size_t generate_command_request(char* buffer, size_t buffer_size, const char* command, JsonVariant payload = JsonVariant()) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["command"] = command;
    if (!payload.isNull()) {
        obj["payload"] = payload;
    }
    
    // Serialize to MessagePack format
    return serializeMsgPack(doc, buffer, buffer_size);
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

        // Initialize and connect to the server
        TLS_CLIENT_HANDLE handle = tls_client_init_and_connect(
            TLS_CLIENT_SERVER,
            TLS_CLIENT_PORT,
            cert_ok,
            sizeof(cert_ok)
        );

        if (!handle) {
            printf("Failed to connect to TLS server\n");
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retry
            continue;
        }

        // First send authentication request
        size_t auth_len = generate_auth_request(message_buffer, sizeof(message_buffer));
		message_buffer[auth_len] = '\0';

        printf("Sending auth request (%d bytes)\n", auth_len);

        // Send auth request and receive response
        int recv_len = tls_client_send_and_recv(
            handle,
            (uint8_t*)message_buffer,
            auth_len,
            response_buffer,
            sizeof(response_buffer),
            5000  // 5 second timeout for auth
        );

        if (recv_len <= 0) {
            printf("Error receiving auth response: %d\n", recv_len);
            tls_client_close(handle);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        response_buffer[recv_len] = '\0';
        // printf("Auth response received (%d bytes): %s\n", recv_len, response);

        // Deserialize the MessagePack response
        JsonDocument response_doc;
        DeserializationError error = deserializeMsgPack(response_doc, response_buffer, recv_len);
        message_buffer[0] = '\0';

        if (error) {
            printf("MessagePack deserialization failed: %s\n", error.c_str());
        } else {
            printf("Deserialized response:\n");
            serializeJsonPretty(response_doc, message_buffer, sizeof(message_buffer));
            printf("%s\n", message_buffer);
        }
        response_doc.clear();

        // Prepare array of commands to send
        struct Command {
            const char* name;
            JsonVariant payload;
        };

        Command commands[] = {
            {"ping", JsonVariant()},
            {"get_time", JsonVariant()}
        };

        // Send each command one by one
        for (const auto& cmd : commands) {
            size_t cmd_len = generate_command_request(message_buffer, sizeof(message_buffer), cmd.name, cmd.payload);
			message_buffer[cmd_len] = '\0';

            printf("Sending command '%s' (%d bytes)\n", cmd.name, cmd_len);

            recv_len = tls_client_send_and_recv(
                handle,
                (uint8_t*)message_buffer,
                cmd_len,
                response_buffer,
                sizeof(response_buffer),
                5000  // 5 second timeout per command
            );

            if (recv_len > 0) {
                response_buffer[recv_len] = '\0';
                // printf("Command response received (%d bytes): %s\n", recv_len, response);
            } else {
                printf("Error receiving command response: %d\n", recv_len);
                break;
            }

            // Deserialize the MessagePack response
            error = deserializeMsgPack(response_doc, response_buffer, recv_len);
            message_buffer[0] = '\0';

            if (error) {
                printf("MessagePack deserialization failed: %s\n", error.c_str());
            } else {
                printf("Deserialized response:\n");
                serializeJsonPretty(response_doc, message_buffer, sizeof(message_buffer));
                printf("%s\n", message_buffer);
            }
            response_doc.clear();

            // Small delay between commands
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Send the some_other_data command separately
        JsonDocument cmd_doc;
        JsonArray arr = cmd_doc.to<JsonArray>();
        arr.add(1);
        arr.add(2);
        arr.add("test");

        size_t cmd_len = generate_command_request(message_buffer, sizeof(message_buffer), "some_other_data", arr);
        message_buffer[cmd_len] = '\0';

        printf("Sending command 'some_other_data' (%d bytes)\n", cmd_len);

        recv_len = tls_client_send_and_recv(
            handle,
            (uint8_t*)message_buffer,
            cmd_len,
            response_buffer,
            sizeof(response_buffer),
            5000  // 5 second timeout per command
        );

        if (recv_len > 0) {
            response_buffer[recv_len] = '\0';
            // printf("Command response received (%d bytes): %s\n", recv_len, response);
        } else {
            printf("Error receiving command response: %d\n", recv_len);
        }

        // Deserialize the MessagePack response
        error = deserializeMsgPack(response_doc, response_buffer, recv_len);
        message_buffer[0] = '\0';

        if (error) {
            printf("MessagePack deserialization failed: %s\n", error.c_str());
        } else {
            printf("Deserialized response:\n");
            serializeJsonPretty(response_doc, message_buffer, sizeof(message_buffer));
            printf("%s\n", message_buffer);
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
    StockData stock_data;
    initialize_stock_data(stock_data);

    // start the led blinking
    xTaskCreate(blink_task, "BlinkThread", BLINK_TASK_STACK_SIZE, NULL,
                BLINK_TASK_PRIORITY, NULL);
    xTaskCreate(wifi_task, "WiFiThread", WIFI_TASK_STACK_SIZE, NULL,
                WIFI_TASK_PRIORITY, NULL);
    xTaskCreate(tls_client_task, "TLSClientThread", HTTP_GET_TASK_STACK_SIZE, NULL,
                HTTP_GET_TASK_PRIORITY, NULL);

    while (true) {
        static int last_core_id = -1;
        if (portGET_CORE_ID() != last_core_id) {
            last_core_id = portGET_CORE_ID();
            printf("main task is on core %d\n", last_core_id);
        }

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

    // 1. Create a datetime_t struct to hold the time
    datetime_t t = {
        .year = 2025,
        .month = 6,
        .day = 6,
        .dotw = 5, // 0:Sun, 1:Mon, 2:Tue, 3:Wed, 4:Thu, 5:Fri, 6:Sat
        .hour = 12,
        .min = 30,
        .sec = 15
    };

    // 2. Initialize the RTC and set the time
    rtc_init();
    rtc_set_datetime(&t);
    

	printf("Starting %s on both cores:\n", rtos_name);
	vLaunch();
	return 0;
}
