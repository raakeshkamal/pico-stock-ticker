#include "hardware/spi.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <cstdint>
#include <cstdio>
#include <cyw43.h>
#include <stdio.h>

#include <cstdlib>
#include <math.h>
#include <string.h>
#include <vector>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "button.hpp"
#include "drivers/st7789/st7789.hpp"
#include "libraries/pico_display_2/pico_display_2.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "rgbled.hpp"

#include "lwip/netif.h"
#include "lwip/altcp_tls.h"

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *api_key = API_KEY;

#define HOST "www.alphavantage.co"
#define URL_REQUEST "/query?function=CURRENCY_EXCHANGE_RATE&from_currency=USD&to_currency=JPY&apikey="  API_KEY

const uint32_t SPI_FREQ = 1000 * 1000;
const uint32_t timeout = 30000;

const uint32_t LED_DELAY = 100;

// Priorities of our threads - higher numbers are higher priority
#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
#define BLINK_TASK_PRIORITY (tskIDLE_PRIORITY + 1UL)
#define WIFI_TASK_PRIORITY (tskIDLE_PRIORITY + 3UL)
#define HTTP_GET_TASK_PRIORITY (tskIDLE_PRIORITY + 4UL)

// Stack sizes of our threads in words (4 bytes)
#define MAIN_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define BLINK_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define WIFI_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
#define HTTP_GET_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 10)

#define ROOT_CERT "-----BEGIN CERTIFICATE-----\n\
MIIDszCCApugAwIBAgIUbuK+gRCgScq3OcxJO6tPWDFrrAMwDQYJKoZIhvcNAQEL\n\
BQAwaTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExFTATBgNVBAcM\n\
DE1vdW50YWluVmlldzEOMAwGA1UECgwFTXlPcmcxCzAJBgNVBAsMAkNBMREwDwYD\n\
VQQDDAhNeVRlc3RDQTAeFw0yNTA2MDQyMTIzNTBaFw0zNTA2MDIyMTIzNTBaMGkx\n\
CzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQHDAxNb3Vu\n\
dGFpblZpZXcxDjAMBgNVBAoMBU15T3JnMQswCQYDVQQLDAJDQTERMA8GA1UEAwwI\n\
TXlUZXN0Q0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCpTBO2se2N\n\
NfP2AS3Dp//yrOIhw5pUVQdpnPTlo4GszNClU9Q1RT7sQjZEinIntJr2TNmrvF70\n\
iEGxrc1DOGwNOavD22g5CtwP/m1ZXzlGFUl5R9NVgp3EfxCok79cmhBr7oVYOiIj\n\
zxXQnkEXats3+mUww1HL6UvknjUdL71MlMwBsogLfN0AMO0pPO5LEs89kCvvGOzJ\n\
y9xLTp0pZcVXuFNrIEUJLLsEgRLhvlXVAukCXaZKGfBvsF+5yKBZ76Qn7rVah21v\n\
xtumxMI4PxAZofbEOFEfq01351uKFSErcu6runJlizHJLN3sS3fUtd/XxqCloZM7\n\
JT5TbkIIgYt3AgMBAAGjUzBRMB0GA1UdDgQWBBQyaqJsRfo30h0B8wC72AUMzDMK\n\
LTAfBgNVHSMEGDAWgBQyaqJsRfo30h0B8wC72AUMzDMKLTAPBgNVHRMBAf8EBTAD\n\
AQH/MA0GCSqGSIb3DQEBCwUAA4IBAQAuQU8ceYQ8TVI7ieIq7wCb/gHxfLHCIfYB\n\
mhjI3PSzhHPQuvFhgfPUDg8Of5ekv05bVD3JbxSVyAce69iHKGLoog8BzvSBK6uC\n\
4BgBB5RSmv7u1FHTPfGr99rqJdleNQWV5EnI712jARceiX6UxZbMZVGFrD+vpDT5\n\
2qnC9Sgmdb0/up7/jul7aVWzeXi95wPXoafRjrHe6xxIo+qbZST0foHUuOC0Jnya\n\
Rdte6KoWMtSzZxA4TSEy1FKBBdPsNrbH/iNCO0pTQ1eOEUAZJwkGCkA4w5XGyHzO\n\
MrczL/37SegB2zR/oBqGCVTIZupsgkUFhu7WfINKE2IhtScP7ldf\n\
-----END CERTIFICATE-----\n"
