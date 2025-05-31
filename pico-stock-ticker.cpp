#include "pico-stock-ticker.hpp"
using namespace pimoroni;

ST7789 st7789(320, 240, ROTATE_0, false, get_spi_pins(BG_SPI_FRONT));
PicoGraphics_PenRGB332 graphics(st7789.width, st7789.height, nullptr);

RGBLED led(PicoDisplay2::LED_R, PicoDisplay2::LED_G, PicoDisplay2::LED_B);

Button button_a(PicoDisplay2::A);
Button button_b(PicoDisplay2::B);
Button button_x(PicoDisplay2::X);
Button button_y(PicoDisplay2::Y);

static const uint8_t cert_ok[] = ROOT_CERT;

static EXAMPLE_HTTP_REQUEST_T async_http_request_state;
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

static void http_client_async_result_cb(void *arg, httpc_result_t httpc_result,
                                        u32_t rx_content_len, u32_t srv_res,
                                        err_t err) {
	EXAMPLE_HTTP_REQUEST_T *req = (EXAMPLE_HTTP_REQUEST_T *)arg;
	printf("Async HTTP request finished.\n");
	printf("HTTP Result: %d\n", httpc_result);
	printf("Received Content Length: %lu\n", rx_content_len);
	printf("Server Response Code: %lu\n", srv_res);
	printf("LwIP Error: %d\n", err);

	if (req->user_semaphore != NULL) {
		xSemaphoreGiveFromISR((SemaphoreHandle_t)req->user_semaphore, NULL);
	}
}

void http_get_task(__unused void *params) {
	printf("http_get_task starts\n");

	printf("http_get_task: Waiting for Wi-Fi connection...\n");
	xSemaphoreTake(wifi_connected_sem, portMAX_DELAY);
	printf("http_get_task: Wi-Fi connected. Proceeding with HTTP requests.\n");

	while (true) {
		static int last_core_id = -1;
		if (portGET_CORE_ID() != last_core_id) {
			last_core_id = portGET_CORE_ID();
			printf("http_get_task is on core %d\n", last_core_id);
		}

		memset(&async_http_request_state, 0, sizeof(async_http_request_state));
		async_http_request_state.hostname = HOST;
		async_http_request_state.url = URL_REQUEST;
		async_http_request_state.headers_fn = http_client_header_print_fn;
		async_http_request_state.recv_fn = http_client_receive_print_fn;
		async_http_request_state.result_fn = http_client_async_result_cb;
		async_http_request_state.callback_arg = &async_http_request_state;
		async_http_request_state.user_semaphore = http_request_complete_sem;

		async_http_request_state.tls_config =
		    altcp_tls_create_config_client(cert_ok, sizeof(cert_ok));
		if (async_http_request_state.tls_config == NULL) {
			printf("http_get_task: Failed to create TLS config\n");
			vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retrying
			continue;
		}

		printf(
		    "http_get_task: Starting asynchronous HTTPS GET request to %s%s\n",
		    async_http_request_state.hostname, async_http_request_state.url);

		xSemaphoreTake(http_request_complete_sem, 0);

		err_t err = http_client_request_async(cyw43_arch_async_context(),
		                                      &async_http_request_state);

		if (err != ERR_OK) {
			printf("http_get_task: Failed to start async HTTP request: %d\n",
			       err);
			altcp_tls_free_config(async_http_request_state.tls_config);
			async_http_request_state.tls_config = NULL;
		} else {
			printf("http_get_task: Async request initiated. Waiting for "
			       "completion semaphore...\n");
			// Wait for the callback to signal completion, with a timeout
			if (xSemaphoreTake(http_request_complete_sem,
			                   pdMS_TO_TICKS(30000)) == pdTRUE) {
				printf(
				    "http_get_task: Semaphore received. Request completed.\n");
				if (async_http_request_state.result == HTTPC_RESULT_OK &&
				    async_http_request_state.complete) {
					printf("http_get_task: Request successful!\n");
				} else {
					printf("http_get_task: Request failed or incomplete. "
					       "Result: %d, Complete: %d\n",
					       async_http_request_state.result,
					       async_http_request_state.complete);
				}
			} else {
				printf("http_get_task: HTTP request timed out!\n");
			}
			if (async_http_request_state.tls_config) {
				altcp_tls_free_config(async_http_request_state.tls_config);
				async_http_request_state.tls_config = NULL;
			}
		}

		// Delay before making the next request
		printf(
		    "http_get_task: Waiting for 60 seconds before next request...\n");
		vTaskDelay(pdMS_TO_TICKS(60000));
	}

	vSemaphoreDelete(http_request_complete_sem);
	vTaskDelete(NULL);
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

	// start the led blinking
	xTaskCreate(blink_task, "BlinkThread", BLINK_TASK_STACK_SIZE, NULL,
	            BLINK_TASK_PRIORITY, NULL);
	xTaskCreate(wifi_task, "WiFiThread", WIFI_TASK_STACK_SIZE, NULL,
	            WIFI_TASK_PRIORITY, NULL);

	if (http_request_complete_sem != NULL && wifi_connected_sem != NULL) {
		xTaskCreate(http_get_task, "HTTPGetThread", HTTP_GET_TASK_STACK_SIZE,
		            NULL, HTTP_GET_TASK_PRIORITY, NULL);
	} else {
		printf(
		    "Not starting HTTPGetThread due to semaphore creation failure.\n");
	}

	st7789.set_backlight(255);

	struct pt {
		float x;
		float y;
		uint8_t r;
		float dx;
		float dy;
		uint16_t pen;
	};

	std::vector<pt> shapes;
	for (int i = 0; i < 100; i++) {
		pt shape;
		shape.x = rand() % graphics.bounds.w;
		shape.y = rand() % graphics.bounds.h;
		shape.r = (rand() % 10) + 3;
		shape.dx = float(rand() % 255) / 64.0f;
		shape.dy = float(rand() % 255) / 64.0f;
		shape.pen =
		    graphics.create_pen(rand() % 255, rand() % 255, rand() % 255);
		shapes.push_back(shape);
	}

	Point text_location(0, 0);

	Pen BG = graphics.create_pen(120, 40, 60);
	Pen WHITE = graphics.create_pen(255, 255, 255);
	while (true) {
		static int last_core_id = -1;
		if (portGET_CORE_ID() != last_core_id) {
			last_core_id = portGET_CORE_ID();
			printf("main task is on core %d\n", last_core_id);
		}
		if (button_a.raw())
			text_location.x -= 1;
		if (button_b.raw())
			text_location.x += 1;

		if (button_x.raw())
			text_location.y -= 1;
		if (button_y.raw())
			text_location.y += 1;

		graphics.set_pen(BG);
		graphics.clear();

		for (auto &shape : shapes) {
			shape.x += shape.dx;
			shape.y += shape.dy;
			if ((shape.x - shape.r) < 0) {
				shape.dx *= -1;
				shape.x = shape.r;
			}
			if ((shape.x + shape.r) >= graphics.bounds.w) {
				shape.dx *= -1;
				shape.x = graphics.bounds.w - shape.r;
			}
			if ((shape.y - shape.r) < 0) {
				shape.dy *= -1;
				shape.y = shape.r;
			}
			if ((shape.y + shape.r) >= graphics.bounds.h) {
				shape.dy *= -1;
				shape.y = graphics.bounds.h - shape.r;
			}

			graphics.set_pen(shape.pen);
			graphics.circle(Point(shape.x, shape.y), shape.r);
		}

		// Since HSV takes a float from 0.0 to 1.0 indicating hue,
		// then we can divide millis by the number of milliseconds
		// we want a full colour cycle to take. 5000 = 5 sec
		RGB p = RGB::from_hsv((float)millis() / 5000.0f, 1.0f,
		                      0.5f + sinf(millis() / 100.0f / 3.14159f) * 0.5f);

		led.set_rgb(p.r, p.g, p.b);

		graphics.set_pen(WHITE);
		graphics.text("Hello World", text_location, 320);

		// update screen
		st7789.update(&graphics);
		vTaskDelay(10); // TODO: create var
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

	stdio_init_all();

	printf("Starting %s on both cores:\n", rtos_name);
	vLaunch();
	return 0;
}
