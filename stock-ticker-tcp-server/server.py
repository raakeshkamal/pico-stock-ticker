import socket
import ssl
import threading
import msgpack
import os
import logging
import time

# Configure logging
logging.basicConfig(
    level=logging.INFO, # Set to logging.DEBUG for even more, if libraries use it
    format="%(asctime)s - %(levelname)s - %(threadName)s - %(message)s",
)

# Server configuration
HOST = "0.0.0.0"  # Listen on all available interfaces
PORT = 8443
CERT_DIR = "./certs/server"
SERVER_CERT = os.path.join(CERT_DIR, "server.crt")
SERVER_KEY = os.path.join(CERT_DIR, "server.key")

VALID_CLIENT_TOKEN = os.environ.get("CLIENT_AUTH_TOKEN")
if not VALID_CLIENT_TOKEN:
    logging.error(
        "CLIENT_AUTH_TOKEN environment variable not set. Server cannot start."
    )
    exit(1)

def handle_client(secure_conn, client_address):
    logging.info(f"Handling client from {client_address} on secure connection.")
    unpacker = msgpack.Unpacker(raw=False)
    packer = msgpack.Packer()

    try:
        # 1. Client Authentication
        logging.info(f"[{client_address}] Waiting for client token...")
        auth_data = secure_conn.recv(1024)
        if not auth_data:
            logging.warning(f"[{client_address}] Disconnected before sending token.")
            return
        logging.info(f"Received auth data: {auth_data}")
        unpacker.feed(auth_data)
        try:
            auth_message = unpacker.unpack()
            logging.debug(f"[{client_address}] Received auth data: {auth_message}")
        except msgpack.OutOfData:
            logging.error(f"[{client_address}] Incomplete auth message.")
            secure_conn.sendall(packer.pack({"status": "error", "message": "Incomplete token message"}))
            return

        if isinstance(auth_message, dict) and "token" in auth_message:
            client_token = auth_message["token"]
            if client_token == VALID_CLIENT_TOKEN:
                logging.info(f"[{client_address}] Authenticated successfully.")
                secure_conn.sendall(packer.pack({"status": "ok", "message": "Authenticated"}))
            else:
                logging.warning(f"[{client_address}] Invalid token. Denying access.")
                secure_conn.sendall(packer.pack({"status": "error", "message": "Invalid token"}))
                return
        else:
            logging.warning(f"[{client_address}] Malformed auth message. Denying access.")
            secure_conn.sendall(packer.pack({"status": "error", "message": "Malformed token message"}))
            return

        # 2. Main communication loop
        logging.info(f"[{client_address}] Entering main communication loop.")
        while True:
            data = secure_conn.recv(4096)
            if not data:
                logging.info(f"[{client_address}] Client disconnected (recv returned no data).")
                break
            
            logging.debug(f"[{client_address}] Received {len(data)} bytes of raw data.")
            unpacker.feed(data)
            for message in unpacker:
                logging.info(f"[{client_address}] Received MsgPack: {message}")
                
                if isinstance(message, dict) and "command" in message:
                    response = {
                        "status": "received",
                        "original_command": message["command"],
                        "timestamp": time.time()
                    }
                    if message["command"] == "get_time":
                        response["server_time"] = time.strftime("%Y-%m-%d %H:%M:%S %Z")
                    elif message["command"] == "ping":
                        response["payload"] = "pong"
                else:
                    response = {"status": "error", "message": "Unknown message format"}

                logging.info(f"[{client_address}] Sending MsgPack: {response}")
                secure_conn.sendall(packer.pack(response))
                logging.debug(f"[{client_address}] Sent response.")

    except ssl.SSLError as e:
        # SSLError can occur during recv/send if TLS session has issues post-handshake
        logging.error(f"SSL Error with {client_address} during communication: {e}")
    except msgpack.exceptions.UnpackException as e:
        logging.error(f"MessagePack Unpack Error from {client_address}: {e}")
    except socket.error as e: # Catches BrokenPipeError, ConnectionResetError etc.
        logging.error(f"Socket Error with {client_address}: {e}")
    except Exception as e:
        logging.error(f"Unexpected error with {client_address}: {e}", exc_info=True)
    finally:
        logging.info(f"Closing connection and handler for {client_address}.")
        if secure_conn:
            try:
                secure_conn.shutdown(socket.SHUT_RDWR)
            except (OSError, socket.error) as sd_err: # Can happen if already closed
                logging.debug(f"[{client_address}] Error during shutdown (ignorable): {sd_err}")
            finally:
                secure_conn.close()
        logging.info(f"Client handler for {client_address} finished.")


def main():
    logging.info("Starting server setup...")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    logging.info(f"SSLContext created with protocol: {context.protocol}")

    try:
        context.load_cert_chain(certfile=SERVER_CERT, keyfile=SERVER_KEY)
        logging.info(f"Server certificate '{SERVER_CERT}' and key '{SERVER_KEY}' loaded.")
    except FileNotFoundError:
        logging.error(
            f"Error: Certificate or key file not found. "
            f"Checked: {SERVER_CERT}, {SERVER_KEY}. "
            f"Ensure certs are correctly mounted into {CERT_DIR}."
        )
        return
    except ssl.SSLError as e:
        logging.error(f"SSL context certificate setup error: {e}")
        return
    
    # Explicitly set ciphers
    PICO_COMPATIBLE_RSA_CIPHERS = [
        "AES256-GCM-SHA384",
        "AES128-GCM-SHA256",
        "AES256-SHA256",
        "AES128-SHA256",
        "AES256-SHA",
        "AES128-SHA",
        # Modern OpenSSL might prefer ECDHE-RSA if available and Pico supports it
        # "ECDHE-RSA-AES256-GCM-SHA384", # Requires Pico to offer ECDHE-RSA
        # "ECDHE-RSA-AES128-GCM-SHA256", # Requires Pico to offer ECDHE-RSA
    ]
    cipher_string = ':'.join(PICO_COMPATIBLE_RSA_CIPHERS)
    try:
        context.set_ciphers(cipher_string)
        logging.info(f"Server ciphers explicitly set to: {cipher_string}")
        # Log all ciphers enabled in the context (can be many due to OpenSSL defaults + our list)
        # enabled_ciphers = context.get_ciphers()
        # logging.debug(f"Full list of ciphers enabled in context: {enabled_ciphers}")
    except ssl.SSLError as e:
        logging.error(f"Error setting ciphers '{cipher_string}': {e}. Your OpenSSL version might not support all of them.")
        # Fallback or exit if critical
        # context.set_ciphers("DEFAULT") 
        # logging.warning("Fell back to DEFAULT ciphers due to error.")


    # Optional: Set TLS options (e.g., disable older protocols if necessary)
    # context.options |= ssl.OP_NO_TLSv1 | ssl.OP_NO_TLSv1_1 # Example: Require TLSv1.2+

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        # Allow address reuse
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((HOST, PORT))
        sock.listen(5)
        logging.info(f"Server bound to {HOST}:{PORT} and listening for connections...")

        while True:
            conn = None # Ensure conn is defined for finally block
            secure_conn = None
            client_address = None
            try:
                logging.debug("Waiting to accept a new raw TCP connection...")
                conn, client_address = sock.accept()
                logging.info(f"Accepted raw TCP connection from {client_address}")

                logging.info(f"Attempting TLS handshake with {client_address}...")
                secure_conn = context.wrap_socket(conn, server_side=True)
                
                # Log details of the successful TLS session
                tls_version = secure_conn.version()
                negotiated_cipher = secure_conn.cipher()
                logging.info(
                    f"TLS handshake successful with {client_address}. "
                    f"Version: {tls_version}, Cipher: {negotiated_cipher}"
                )
                
                client_thread = threading.Thread(
                    target=handle_client, 
                    args=(secure_conn, client_address),
                    name=f"ClientThread-{client_address[0]}-{client_address[1]}"
                )
                client_thread.daemon = True
                client_thread.start()
                logging.info(f"Started client handler thread for {client_address}")

            except ssl.SSLError as e:
                # This is where handshake failures (like NO_SHARED_CIPHER) are caught
                logging.error(f"SSL handshake error with {client_address or 'Unknown Client'}: {e}")
                if secure_conn:
                    secure_conn.close()
                elif conn: # If wrap_socket failed, conn is the raw socket
                    conn.close()
            except Exception as e:
                logging.error(f"Error accepting or wrapping connection from {client_address or 'Unknown Client'}: {e}", exc_info=True)
                if secure_conn:
                    secure_conn.close()
                elif conn:
                    conn.close()

if __name__ == "__main__":
    main()