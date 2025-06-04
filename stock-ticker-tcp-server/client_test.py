import socket
import ssl
import msgpack
import time
import logging
import os

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)

# Client Configuration
# If server is on VPS, change to its domain name (must match CN in server.crt)
# For local Docker test, 'localhost' is fine if server.crt CN is 'localhost'
SERVER_HOST = "192.168.0.41"
SERVER_PORT = 8443
# Path to the CA certificate file the client uses to verify the server
CA_CERT = "./certs/ca/ca.crt" # Make sure this file exists where you run the client

# The token this client will use to authenticate
CLIENT_TOKEN = "supersecretclienttoken12345abcdef" # Must match server's expected token
# CLIENT_TOKEN = "wrongtoken" # For testing authentication failure

def run_client():
    # SSL Context for TLS client
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = True # Enforce hostname checking
    context.verify_mode = ssl.CERT_REQUIRED
    try:
        context.load_verify_locations(cafile=CA_CERT)
        logging.info(f"Loaded CA certificate from {CA_CERT}")
    except FileNotFoundError:
        logging.error(f"CA certificate file not found: {CA_CERT}")
        return
    except ssl.SSLError as e:
        logging.error(f"Error loading CA certificate: {e}")
        return

    packer = msgpack.Packer()
    unpacker = msgpack.Unpacker(raw=False)

    try:
        # Create a standard socket
        with socket.create_connection((SERVER_HOST, SERVER_PORT), timeout=10) as sock:
            # Wrap the socket with SSL/TLS
            # server_hostname must match the CN or SAN in the server's certificate
            with context.wrap_socket(sock, server_hostname=SERVER_HOST) as secure_sock:
                logging.info(f"Connected to server: {SERVER_HOST}:{SERVER_PORT} using TLS")
                server_cert = secure_sock.getpeercert()
                logging.info(f"Server certificate: {server_cert['subject']}")

                # 1. Send authentication token
                auth_message = {"token": CLIENT_TOKEN}
                logging.info(f"Sending token: {auth_message}")
                secure_sock.sendall(packer.pack(auth_message))

                # Wait for authentication response
                auth_response_data = secure_sock.recv(1024)
                if not auth_response_data:
                    logging.error("Server closed connection after token send.")
                    return
                
                unpacker.feed(auth_response_data)
                try:
                    auth_status = unpacker.unpack()
                    logging.info(f"Auth response: {auth_status}")
                    if not (isinstance(auth_status, dict) and auth_status.get("status") == "ok"):
                        logging.error(f"Authentication failed: {auth_status.get('message', 'Unknown error')}")
                        return
                except msgpack.OutOfData:
                    logging.error("Incomplete authentication response from server.")
                    return


                # 2. Send some test messages
                messages_to_send = [
                    {"command": "ping"},
                    {"command": "get_time"},
                    {"command": "some_other_data", "payload": [1, 2, "test"]},
                    "just a string" # Example of a message that might be considered malformed by server
                ]

                for msg_data in messages_to_send:
                    logging.info(f"Sending: {msg_data}")
                    secure_sock.sendall(packer.pack(msg_data))
                    
                    # Receive response
                    # Loop to handle potentially fragmented messages or multiple messages
                    while True:
                        try:
                            response_data = secure_sock.recv(4096)
                            if not response_data:
                                logging.warning("Server closed connection or no more data.")
                                break # Break inner loop if connection closed
                            
                            unpacker.feed(response_data)
                            for response in unpacker: # Process all fully received messages
                                logging.info(f"Received: {response}")
                            break # Assuming one response per request for this simple client
                        except msgpack.OutOfData:
                            # Not enough data for a full message yet, try receiving more
                            logging.debug("Waiting for more data for a complete msgpack object...")
                            continue 
                        except socket.timeout:
                            logging.warning("Socket timeout waiting for response.")
                            break # Break inner loop on timeout
                    
                    time.sleep(1) # Small delay between messages

    except socket.timeout:
        logging.error(f"Connection to {SERVER_HOST}:{SERVER_PORT} timed out.")
    except socket.gaierror:
        logging.error(f"Could not resolve hostname: {SERVER_HOST}")
    except ConnectionRefusedError:
        logging.error(f"Connection to {SERVER_HOST}:{SERVER_PORT} refused. Is the server running?")
    except ssl.SSLCertVerificationError as e:
        logging.error(f"SSL Certificate Verification Error: {e}. Ensure CN in server cert matches '{SERVER_HOST}' and CA cert is correct.")
    except ssl.SSLError as e:
        logging.error(f"SSL Error: {e}")
    except Exception as e:
        logging.error(f"An unexpected error occurred: {e}", exc_info=True)
    finally:
        logging.info("Client finished.")

if __name__ == "__main__":

    run_client()