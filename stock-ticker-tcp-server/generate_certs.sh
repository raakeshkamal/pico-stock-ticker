#!/bin/bash

# Exit on error
set -e

# Configuration
DAYS_VALID=3650 # 10 years for simplicity, adjust as needed
CA_SUBJ="/C=US/ST=California/L=MountainView/O=MyOrg/OU=CA/CN=MyTestCA"
SERVER_SUBJ="/C=US/ST=California/L=MountainView/O=MyOrg/OU=Server/CN=server.example.com" # IMPORTANT: CN for server
CLIENT_SUBJ="/C=US/ST=California/L=MountainView/O=MyOrg/OU=Client/CN=pico_client_1"     # IMPORTANT: CN for client

# Directories
CA_DIR="certs/ca"
SERVER_DIR="certs/server"
CLIENT_DIR="certs/client"

mkdir -p $CA_DIR $SERVER_DIR $CLIENT_DIR

echo "--- Generating CA ---"
openssl genrsa -out $CA_DIR/ca.key 4096
openssl req -new -x509 -days $DAYS_VALID -key $CA_DIR/ca.key -out $CA_DIR/ca.crt -subj "$CA_SUBJ"
echo "CA Key: $CA_DIR/ca.key"
echo "CA Cert: $CA_DIR/ca.crt"

echo ""
echo "--- Generating Server Certificate ---"
openssl genrsa -out $SERVER_DIR/server.key 2048
openssl req -new -key $SERVER_DIR/server.key -out $SERVER_DIR/server.csr -subj "$SERVER_SUBJ"
openssl x509 -req -days $DAYS_VALID -in $SERVER_DIR/server.csr -CA $CA_DIR/ca.crt -CAkey $CA_DIR/ca.key -CAcreateserial -out $SERVER_DIR/server.crt
# For pico_lwip_mbedtls, DER format might be easier for the client cert and key
# openssl x509 -in $SERVER_DIR/server.crt -out $SERVER_DIR/server.der -outform DER
# openssl rsa -in $SERVER_DIR/server.key -out $SERVER_DIR/server_key.der -outform DER
echo "Server Key: $SERVER_DIR/server.key"
echo "Server Cert: $SERVER_DIR/server.crt"
echo "Server CSR: $SERVER_DIR/server.csr"

echo ""
echo "--- Generating Client Certificate ---"
openssl genrsa -out $CLIENT_DIR/client.key 2048
openssl req -new -key $CLIENT_DIR/client.key -out $CLIENT_DIR/client.csr -subj "$CLIENT_SUBJ"
openssl x509 -req -days $DAYS_VALID -in $CLIENT_DIR/client.csr -CA $CA_DIR/ca.crt -CAkey $CA_DIR/ca.key -CAserial $CA_DIR/ca.srl -out $CLIENT_DIR/client.crt # Use same serial file
# For pico_lwip_mbedtls, DER format might be easier for the client cert and key
# openssl x509 -in $CLIENT_DIR/client.crt -out $CLIENT_DIR/client.der -outform DER
# openssl rsa -in $CLIENT_DIR/client.key -out $CLIENT_DIR/client_key.der -outform DER

echo "Client Key: $CLIENT_DIR/client.key"
echo "Client Cert: $CLIENT_DIR/client.crt"
echo "Client CSR: $CLIENT_DIR/client.csr"

echo ""
echo "--- Verifying Certificates ---"
openssl verify -CAfile $CA_DIR/ca.crt $SERVER_DIR/server.crt
openssl verify -CAfile $CA_DIR/ca.crt $CLIENT_DIR/client.crt

echo ""
echo "Done. Important files:"
echo "CA Cert: $CA_DIR/ca.crt (needed by both server and client to verify each other)"
echo "Server Cert: $SERVER_DIR/server.crt"
echo "Server Key: $SERVER_DIR/server.key"
echo "Client Cert: $CLIENT_DIR/client.crt"
echo "Client Key: $CLIENT_DIR/client.key"
echo ""
echo "For RPi Pico W (mbedTLS):"
echo "  - You'll need $CA_DIR/ca.crt (to verify the server)."
echo "  - You'll need $CLIENT_DIR/client.crt (as its own certificate)."
echo "  - You'll need $CLIENT_DIR/client.key (as its own private key)."
echo "  Consider converting these to DER format if mbedTLS prefers it, or embed as C arrays."