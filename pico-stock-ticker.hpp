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
#define WIFI_TASK_PRIORITY (tskIDLE_PRIORITY + 4UL)
#define HTTP_GET_TASK_PRIORITY (tskIDLE_PRIORITY + 3UL)

// Stack sizes of our threads in words (4 bytes)
#define MAIN_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define BLINK_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define WIFI_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 2)
#define HTTP_GET_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 3)

#define ROOT_CERT "-----BEGIN CERTIFICATE-----\n\
MIIDsTCCA1igAwIBAgIRAJCgJCCDMEWmE3GiymQB0wEwCgYIKoZIzj0EAwIwOzEL\n\
MAkGA1UEBhMCVVMxHjAcBgNVBAoTFUdvb2dsZSBUcnVzdCBTZXJ2aWNlczEMMAoG\n\
A1UEAxMDV0UxMB4XDTI1MDQyOTE1Mzg0MloXDTI1MDcyODE2MzgzOFowGjEYMBYG\n\
A1UEAxMPYWxwaGF2YW50YWdlLmNvMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE\n\
ikvHFSWZQArtPinZolDNNXq6Mghy7hcBeKW8yKfyh+5lhzSjUAOA3ys6OdKOsJRb\n\
9MAPcF4tXHevo9Hqv0MgZaOCAlwwggJYMA4GA1UdDwEB/wQEAwIHgDATBgNVHSUE\n\
DDAKBggrBgEFBQcDATAMBgNVHRMBAf8EAjAAMB0GA1UdDgQWBBTVdo8+NXd9kgU9\n\
ToPh51v+l3d1tzAfBgNVHSMEGDAWgBSQd5I1Z8T/qMyp5nvZgHl7zJP5ODBeBggr\n\
BgEFBQcBAQRSMFAwJwYIKwYBBQUHMAGGG2h0dHA6Ly9vLnBraS5nb29nL3Mvd2Ux\n\
L2tLQTAlBggrBgEFBQcwAoYZaHR0cDovL2kucGtpLmdvb2cvd2UxLmNydDAtBgNV\n\
HREEJjAkgg9hbHBoYXZhbnRhZ2UuY2+CESouYWxwaGF2YW50YWdlLmNvMBMGA1Ud\n\
IAQMMAowCAYGZ4EMAQIBMDYGA1UdHwQvMC0wK6ApoCeGJWh0dHA6Ly9jLnBraS5n\n\
b29nL3dlMS9wSUcxRmo5SFpFdy5jcmwwggEFBgorBgEEAdZ5AgQCBIH2BIHzAPEA\n\
dwDd3Mo0ldfhFgXnlTL6x5/4PRxQ39sAOhQSdgosrLvIKgAAAZaCajz5AAAEAwBI\n\
MEYCIQCRg4S/6xYpQaYK5Ys/oJZLSip+dznkMtdRRs2TXNVdmQIhALvOkEdcwtw0\n\
3Xp4munbcTbQmBaWOPEel7Bn8BuCLZvbAHYAzPsPaoVxCWX+lZtTzumyfCLphVwN\n\
l422qX5UwP5MDbAAAAGWgmo9BwAABAMARzBFAiBh1p4M7LvQGiycntSTUJyyRgDk\n\
y5RN1kcjp70txHyptAIhAIajh85IdAKHnbqaxq4sXb1gStx0vKL+ejTk8dfNQ4pk\n\
MAoGCCqGSM49BAMCA0cAMEQCIDWdiqVYEzuCXqhdzng5caHaZJO9kIF8GkR8MZ1e\n\
+jd9AiBedJEnlgKWxxS65Cbmm1fG2vFJB0C8FtzR4XJgyxImYA==\n\
-----END CERTIFICATE-----\n"
