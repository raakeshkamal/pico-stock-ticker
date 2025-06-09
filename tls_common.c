/*
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <mbedtls/ssl.h>
#include <string.h>

#include "FreeRTOS.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "semphr.h"

#include "mbedtls/debug.h"
#include "tls_client.h"

// Error codes
#define TLS_ERROR_TIMEOUT -1
#define TLS_ERROR_GENERIC -2
#define TLS_ERROR_MEMORY -3
#define TLS_ERROR_CONNECTION -4

typedef struct TLS_CLIENT_T_ {
	struct altcp_pcb *pcb;
	SemaphoreHandle_t complete_sem;
	SemaphoreHandle_t recv_sem;
	int error;
	uint8_t *recv_buffer;
	size_t recv_buffer_size;
	size_t recv_len;
	bool is_connected;
} TLS_CLIENT_T;

static struct altcp_tls_config *tls_config = NULL;
#ifdef MBEDTLS_DEBUG_C
// Example debug callback
void my_debug(void *ctx, int level, const char *file, int line,
              const char *str) {
	((void)ctx);
	printf("%s:%04d: |%d| %s", file, line, level, str);
}
#endif

static err_t tls_client_close_internal(TLS_CLIENT_T *state) {
	err_t err = ERR_OK;

	if (state->complete_sem != NULL) {
		xSemaphoreGive(state->complete_sem);
	}
	if (state->recv_sem != NULL) {
		xSemaphoreGive(state->recv_sem);
	}

	if (state->pcb != NULL) {
		altcp_arg(state->pcb, NULL);
		altcp_poll(state->pcb, NULL, 0);
		altcp_recv(state->pcb, NULL);
		altcp_err(state->pcb, NULL);
		err = altcp_close(state->pcb);
		if (err != ERR_OK) {
			printf("close failed %d, calling abort\n", err);
			altcp_abort(state->pcb);
			err = ERR_ABRT;
		}
		state->pcb = NULL;
	}
	state->is_connected = false;
	return err;
}

static err_t tls_client_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
	TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
	if (err != ERR_OK) {
		printf("connect failed %d\n", err);
		state->error = TLS_ERROR_CONNECTION;
		return tls_client_close_internal(state);
	}

	state->is_connected = true;
	xSemaphoreGive(state->complete_sem);
	return ERR_OK;
}

static err_t tls_client_poll(void *arg, struct altcp_pcb *pcb) {
	TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
	printf("timed out\n");
	state->error = TLS_ERROR_TIMEOUT;
	return tls_client_close_internal(arg);
}

static void tls_client_err(void *arg, err_t err) {
	TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
	printf("tls_client_err %d\n", err);
	state->error = TLS_ERROR_GENERIC;
	tls_client_close_internal(state);
}

static err_t tls_client_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p,
                             err_t err) {
	TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
	if (!p) {
		printf("connection closed\n");
		state->error = TLS_ERROR_CONNECTION;
		return tls_client_close_internal(state);
	}

	if (p->tot_len > 0) {
		if (state->recv_buffer && state->recv_buffer_size > 0) {
			size_t copy_len = p->tot_len > state->recv_buffer_size
			                      ? state->recv_buffer_size
			                      : p->tot_len;
			pbuf_copy_partial(p, state->recv_buffer, copy_len, 0);
			state->recv_len = copy_len;
		}
		altcp_recved(pcb, p->tot_len);
		xSemaphoreGive(state->recv_sem);
	}
	pbuf_free(p);
	return ERR_OK;
}

static void tls_client_connect_to_server_ip(const ip_addr_t *ipaddr,
                                            TLS_CLIENT_T *state,
                                            uint16_t port) {
	err_t err;

#ifdef MBEDTLS_DEBUG_C
	mbedtls_ssl_context *ssl_context = altcp_tls_context(state->pcb);
	mbedtls_ssl_config *ssl_conf = ssl_context->conf;

	mbedtls_ssl_conf_dbg(ssl_conf, my_debug, NULL);
	mbedtls_debug_set_threshold(1);
#endif

	printf("connecting to server IP %s port %d\n", ipaddr_ntoa(ipaddr), port);
	err = altcp_connect(state->pcb, ipaddr, port, tls_client_connected);
	if (err != ERR_OK) {
		printf("error initiating connect, err=%d\n", err);
		state->error = TLS_ERROR_CONNECTION;
		tls_client_close_internal(state);
	}
}

static void tls_client_dns_found(const char *hostname, const ip_addr_t *ipaddr,
                                 void *arg) {
	TLS_CLIENT_T *state = (TLS_CLIENT_T *)arg;
	if (ipaddr) {
		printf("DNS resolving complete\n");
		tls_client_connect_to_server_ip(ipaddr, state,
		                                8443); // TODO: Make port configurable
	} else {
		printf("error resolving hostname %s\n", hostname);
		state->error = TLS_ERROR_CONNECTION;
		tls_client_close_internal(state);
	}
}

static bool tls_client_open(const char *hostname, TLS_CLIENT_T *state) {
	err_t err;
	ip_addr_t server_ip;

	state->pcb = altcp_tls_new(tls_config, IPADDR_TYPE_ANY);
	if (!state->pcb) {
		printf("failed to create pcb\n");
		state->error = TLS_ERROR_MEMORY;
		return false;
	}

	altcp_arg(state->pcb, state);
	altcp_poll(state->pcb, tls_client_poll, 20);
	altcp_recv(state->pcb, tls_client_recv);
	altcp_err(state->pcb, tls_client_err);

	/* Set SNI */
	char *sni = "server.local";
	mbedtls_ssl_set_hostname(altcp_tls_context(state->pcb), sni);

	printf("resolving %s\n", hostname);

	cyw43_arch_lwip_begin();
	err = dns_gethostbyname(hostname, &server_ip, tls_client_dns_found, state);
	if (err == ERR_OK) {
		tls_client_connect_to_server_ip(&server_ip, state,
		                                8443); // TODO: Make port configurable
	} else if (err != ERR_INPROGRESS) {
		printf("error initiating DNS resolving, err=%d\n", err);
		state->error = TLS_ERROR_CONNECTION;
		tls_client_close_internal(state);
	}
	cyw43_arch_lwip_end();

	return err == ERR_OK || err == ERR_INPROGRESS;
}

// New API Implementation

TLS_CLIENT_HANDLE tls_client_init_and_connect(const char *server_hostname,
                                              uint16_t server_port,
                                              const uint8_t *cert,
                                              size_t cert_len) {
	TLS_CLIENT_T *state = calloc(1, sizeof(TLS_CLIENT_T));
	if (!state) {
		printf("failed to allocate state\n");
		return NULL;
	}

	state->complete_sem = xSemaphoreCreateBinary();
	state->recv_sem = xSemaphoreCreateBinary();
	if (!state->complete_sem || !state->recv_sem) {
		printf("failed to create semaphores\n");
		if (state->complete_sem)
			vSemaphoreDelete(state->complete_sem);
		if (state->recv_sem)
			vSemaphoreDelete(state->recv_sem);
		free(state);
		return NULL;
	}

	tls_config = altcp_tls_create_config_client(cert, cert_len);
	if (!tls_config) {
		printf("failed to create TLS config\n");
		vSemaphoreDelete(state->complete_sem);
		vSemaphoreDelete(state->recv_sem);
		free(state);
		return NULL;
	}

	if (!tls_client_open(server_hostname, state)) {
		vSemaphoreDelete(state->complete_sem);
		vSemaphoreDelete(state->recv_sem);
		free(state);
		return NULL;
	}

	// Wait for connection with timeout
	if (xSemaphoreTake(state->complete_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
		printf("Connection timed out\n");
		state->error = TLS_ERROR_TIMEOUT;
		tls_client_close((TLS_CLIENT_HANDLE)state);
		return NULL;
	}

	if (state->error != 0) {
		tls_client_close((TLS_CLIENT_HANDLE)state);
		return NULL;
	}

	return (TLS_CLIENT_HANDLE)state;
}

int tls_client_send_and_recv(TLS_CLIENT_HANDLE handle,
                             const uint8_t *send_buffer, size_t send_len,
                             uint8_t *recv_buffer, size_t recv_buffer_size,
                             uint32_t timeout_ms) {
	TLS_CLIENT_T *state = (TLS_CLIENT_T *)handle;
	if (!state || !state->is_connected) {
		return TLS_ERROR_CONNECTION;
	}

	state->recv_buffer = recv_buffer;
	state->recv_buffer_size = recv_buffer_size;
	state->recv_len = 0;

	err_t err =
	    altcp_write(state->pcb, send_buffer, send_len, TCP_WRITE_FLAG_COPY);
	if (err != ERR_OK) {
		printf("error writing data, err=%d\n", err);
		return TLS_ERROR_GENERIC;
	}

	// Wait for response with timeout
	if (xSemaphoreTake(state->recv_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
		printf("Receive timed out\n");
		return TLS_ERROR_TIMEOUT;
	}

	return state->recv_len;
}

void tls_client_close(TLS_CLIENT_HANDLE handle) {
	if (!handle)
		return;

	TLS_CLIENT_T *state = (TLS_CLIENT_T *)handle;
	tls_client_close_internal(state);

	if (state->complete_sem) {
		vSemaphoreDelete(state->complete_sem);
	}
	if (state->recv_sem) {
		vSemaphoreDelete(state->recv_sem);
	}

	if (tls_config) {
		altcp_tls_free_config(tls_config);
		tls_config = NULL;
	}

	free(state);
}
