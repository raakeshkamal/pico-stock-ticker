import socket
import ssl
import msgpack
import struct
import threading
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)

# Server Configuration
HOST = "0.0.0.0"  # Listen on all available interfaces
PORT = 8443

# Certificate paths (adjust if your Dockerfile places them elsewhere)
# These paths assume the script is run from the root of the project,
# or that the Docker container has these paths.
CA_CERT = "certs/ca/ca.crt"
SERVER_CERT = "certs/server/server.crt"
SERVER_KEY = "certs/server/server.key"


def handle_client(conn, addr):
    logging.info(f"Accepted connection from {addr}")
    client_cert = conn.getpeercert()
    if client_cert:
        logging.info(f"Client certificate: {client_cert.get('subject', 'N/A')}")
    else:
        logging.warning("Client did not present a certificate. Closing.")
        conn.close()
        return

    try:
        while True:
            # 1. Read the 4-byte length prefix (unsigned int, network byte order)
            header_bytes = conn.recv(4)
            if not header_bytes:
                logging.info(f"Client {addr} disconnected (no header).")
                break
            if len(header_bytes) < 4:
                logging.warning(
                    f"Client {addr} sent incomplete header. Disconnecting."
                )
                break

            msg_len = struct.unpack(">I", header_bytes)[0]
            logging.debug(f"Expecting message of length: {msg_len}")

            # 2. Read the MessagePack data
            data = b""
            while len(data) < msg_len:
                chunk = conn.recv(msg_len - len(data))
                if not chunk:
                    logging.warning(
                        f"Client {addr} disconnected during message receive."
                    )
                    return  # Or break, depending on desired behavior
                data += chunk

            if not data:
                logging.info(f"Client {addr} disconnected.")
                break

            # 3. Unpack the message
            try:
                message = msgpack.unpackb(data, raw=False)
                logging.info(f"Received from {addr}: {message}")

                # 4. Process message and prepare response
                response_data = {"status": "received", "echo": message}

            except msgpack.exceptions.ExtraData as e:
                logging.error(f"Extra data from {addr}: {e}. Data: {data!r}")
                response_data = {"status": "error", "message": "Extra data received"}
            except msgpack.UnpackException as e:
                logging.error(f"Invalid MessagePack data from {addr}: {e}")
                response_data = {
                    "status": "error",
                    "message": "Invalid MessagePack format",
                }
            except Exception as e:
                logging.error(f"Error processing message from {addr}: {e}")
                response_data = {"status": "error", "message": str(e)}

            # 5. Pack and send response
            packed_response = msgpack.packb(response_data)
            response_header = struct.pack(">I", len(packed_response))
            conn.sendall(response_header + packed_response)
            logging.info(f"Sent to {addr}: {response_data}")

    except ssl.SSLError as e:
        logging.error(f"SSL Error with {addr}: {e}")
    except ConnectionResetError:
        logging.info(f"Client {addr} reset the connection.")
    except Exception as e:
        logging.error(f"Unhandled exception with client {addr}: {e}")
    finally:
        logging.info(f"Closing connection to {addr}")
        conn.close()


def main():
    # Create SSL context for the server
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.verify_mode = ssl.CERT_REQUIRED  # Require client certificate
    context.load_verify_locations(CA_CERT)  # CA to verify client certs
    context.load_cert_chain(
        certfile=SERVER_CERT, keyfile=SERVER_KEY
    )  # Server's own cert and key

    # For stricter security, you can set ciphers
    # context.set_ciphers('ECDHE+AESGCM:CHACHA20') # Example

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((HOST, PORT))
        sock.listen(5)
        logging.info(f"Server listening on {HOST}:{PORT} with mTLS")

        while True:
            try:
                conn, addr = sock.accept()
                # Wrap the client socket with SSL context
                ssl_conn = context.wrap_socket(conn, server_side=True)
                # Handle each client in a new thread
                client_thread = threading.Thread(
                    target=handle_client, args=(ssl_conn, addr)
                )
                client_thread.daemon = True  # Allow main program to exit
                client_thread.start()
            except ssl.SSLError as e:
                # This can happen if client uses wrong protocol or cert before thread starts
                logging.error(f"SSL Error during accept: {e}")
            except Exception as e:
                logging.error(f"Error accepting connection: {e}")


if __name__ == "__main__":
    main()