#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // for size_t

// Opaque handle for TLS client
typedef struct TLS_CLIENT_T_* TLS_CLIENT_HANDLE;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and open a TLS client connection
 * @param server_hostname The hostname to connect to
 * @param server_port The port to connect to
 * @param cert The server certificate (can be NULL for no verification)
 * @param cert_len Length of the certificate
 * @return Handle to the TLS client if successful, NULL otherwise
 */
TLS_CLIENT_HANDLE tls_client_init_and_connect(const char *server_hostname, uint16_t server_port,
                                            const uint8_t *cert, size_t cert_len);

/**
 * Send data and wait for response
 * @param handle The TLS client handle
 * @param send_buffer The data to send
 * @param send_len Length of the data to send
 * @param recv_buffer Buffer to store the response
 * @param recv_buffer_size Size of the receive buffer
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes received if successful, negative value on error
 */
int tls_client_send_and_recv(TLS_CLIENT_HANDLE handle,
                            const uint8_t *send_buffer, size_t send_len,
                            uint8_t *recv_buffer, size_t recv_buffer_size,
                            uint32_t timeout_ms);

/**
 * Close and deinitialize the TLS client
 * @param handle The TLS client handle to close
 */
void tls_client_close(TLS_CLIENT_HANDLE handle);

/**
 * @brief Run a TLS client test with the given parameters
 * 
 * @param cert The certificate to use for TLS
 * @param cert_len Length of the certificate
 * @param server The server hostname to connect to
 * @param request The HTTP request to send
 * @param timeout Timeout in seconds
 * @return true if the test was successful, false otherwise
 */
bool run_tls_client_test(const uint8_t *cert, size_t cert_len, const char *server, const char *request, int timeout);

#ifdef __cplusplus
}
#endif

#endif // TLS_CLIENT_H 