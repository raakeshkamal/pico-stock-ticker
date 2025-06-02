import socket
import ssl
import msgpack
import struct
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)

# Client Configuration
# If server is on VPS, use its public IP or domain name.
# If server is local Docker, 'localhost' or '127.0.0.1' is fine.
# SERVER_HOST = "server.example.com" # MUST MATCH CN in server's cert
SERVER_HOST = "localhost" # For local testing, if server CN is server.example.com, this will fail hostname check unless you set check_hostname=False (not recommended for prod)
                          # OR, regenerate server cert with CN=localhost for local testing
SERVER_PORT = 8443

# Certificate paths for the client
CA_CERT = "certs/ca/ca.crt"  # To verify the server
CLIENT_CERT = "certs/client/client.crt"  # Client's own certificate
CLIENT_KEY = "certs/client/client.key"  # Client's private key


def main():
    # Create SSL context for the client
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(CA_CERT)  # CA to verify server's cert
    context.load_cert_chain(
        certfile=CLIENT_CERT, keyfile=CLIENT_KEY
    )  # Client's own cert and key

    # Enforce server hostname verification (IMPORTANT!)
    # The server_hostname MUST match the Common Name (CN) or a Subject Alternative Name (SAN)
    # in the server's certificate. For our generated cert, it's "server.example.com".
    # If testing locally and server cert CN is "server.example.com", you might need to
    # add "127.0.0.1 server.example.com" to your /etc/hosts file, or set
    # context.check_hostname = False (NOT recommended for production).
    # For this example, if you run server locally and connect to 'localhost',
    # you should regenerate server cert with CN='localhost' or use /etc/hosts trick.
    # Let's assume you've set CN=localhost for server cert for easy local test:
    # context.check_hostname = True # Default is True
    # server_hostname_to_verify = 'localhost' # if server cert CN is localhost
    server_hostname_to_verify = (
        "server.example.com"  # if server cert CN is server.example.com
    )

    # If you are testing locally and your server cert's CN is 'server.example.com',
    # but you are connecting to 'localhost', you'll get a hostname mismatch.
    # To resolve for local testing:
    # 1. Add '127.0.0.1 server.example.com' to your /etc/hosts file.
    #    Then connect to SERVER_HOST = "server.example.com".
    # 2. OR, for testing only, set context.check_hostname = False (NOT SECURE).
    # 3. OR, regenerate server cert with CN='localhost' if connecting to 'localhost'.

    # For this script, we'll assume you've handled the hostname (e.g., via /etc/hosts)
    # if SERVER_HOST is 'localhost' but cert CN is 'server.example.com'.
    # If SERVER_HOST is already 'server.example.com' and resolves correctly, it's fine.

    try:
        # Create a standard socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Wrap the socket with SSL
        # server_hostname is crucial for SNI and hostname verification
        ssl_sock = context.wrap_socket(
            sock, server_side=False, server_hostname=server_hostname_to_verify
        )

        ssl_sock.connect((SERVER_HOST, SERVER_PORT))
        logging.info(
            f"Connected to server {SERVER_HOST}:{SERVER_PORT} using mTLS"
        )
        server_cert = ssl_sock.getpeercert()
        logging.info(f"Server certificate: {server_cert.get('subject', 'N/A')}")

        # Prepare message
        message_to_send = {
            "command": "PING",
            "payload": "Hello from Python Client!",
            "timestamp": 1234567890,
        }
        packed_message = msgpack.packb(message_to_send)
        header = struct.pack(">I", len(packed_message))

        # Send message
        ssl_sock.sendall(header + packed_message)
        logging.info(f"Sent: {message_to_send}")

        # Receive response
        response_header_bytes = ssl_sock.recv(4)
        if not response_header_bytes or len(response_header_bytes) < 4:
            logging.error("Failed to receive response header or incomplete header.")
            return

        response_msg_len = struct.unpack(">I", response_header_bytes)[0]
        logging.debug(f"Expecting response message of length: {response_msg_len}")

        response_data = b""
        while len(response_data) < response_msg_len:
            chunk = ssl_sock.recv(response_msg_len - len(response_data))
            if not chunk:
                logging.error("Server disconnected during response receive.")
                return
            response_data += chunk

        response = msgpack.unpackb(response_data, raw=False)
        logging.info(f"Received: {response}")

    except ssl.SSLCertVerificationError as e:
        logging.error(
            f"SSL Certificate Verification Error: {e}. "
            f"Ensure SERVER_HOST ('{SERVER_HOST}') matches the server certificate's CN/SAN, "
            f"and the CA cert ('{CA_CERT}') is correct."
        )
    except ssl.SSLError as e:
        logging.error(f"SSL Error: {e}")
    except ConnectionRefusedError:
        logging.error(
            f"Connection refused. Is the server running at {SERVER_HOST}:{SERVER_PORT}?"
        )
    except Exception as e:
        logging.error(f"An error occurred: {e}")
    finally:
        if "ssl_sock" in locals() and ssl_sock:
            ssl_sock.close()
            logging.info("Connection closed.")


if __name__ == "__main__":
    main()