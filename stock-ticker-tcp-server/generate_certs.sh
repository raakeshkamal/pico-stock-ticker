#!/bin/bash

# Exit on error
set -e

# Configuration
DAYS_VALID=3650 # 10 years for simplicity, adjust as needed
CA_SUBJ="/C=US/ST=California/L=MountainView/O=MyOrg/OU=CA/CN=MyTestCA"

# --- IMPORTANT: SERVER CONFIGURATION ---
# Set the Common Name for your server. This is what you'll use for SNI on the Pico.
SERVER_CN="server.local"
# Set the IP address of your server. This will be added to the certificate's SAN.
# This MUST be the IP address your Pico W uses to connect to the server.
SERVER_IP="192.168.0.41" # <<<<====== UPDATE THIS IF YOUR SERVER IP IS DIFFERENT

SERVER_SUBJ="/C=US/ST=California/L=MountainView/O=MyOrg/OU=Server/CN=${SERVER_CN}"
CLIENT_SUBJ="/C=US/ST=California/L=MountainView/O=MyOrg/OU=Client/CN=pico_client_1"

# Directories
CERT_BASE_DIR="certs_$(date +%Y%m%d_%H%M%S)" # Create unique directory for each run
CA_DIR="$CERT_BASE_DIR/ca"
SERVER_DIR="$CERT_BASE_DIR/server"
CLIENT_DIR="$CERT_BASE_DIR/client"

mkdir -p $CA_DIR $SERVER_DIR $CLIENT_DIR

# --- Server OpenSSL Extension Configuration File ---
SERVER_EXT_CONF="$SERVER_DIR/server_ext.cnf"
cat > $SERVER_EXT_CONF << EOF
[ v3_server ]
basicConstraints = CA:FALSE
nsCertType = server
nsComment = "OpenSSL Generated Server Certificate"
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer:always
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = ${SERVER_CN}
IP.1 = ${SERVER_IP}
EOF

echo "--- Generating CA ---"
# Generate 2048-bit RSA key for CA
openssl genrsa -out $CA_DIR/ca.key 2048
openssl req -new -x509 -days $DAYS_VALID -key $CA_DIR/ca.key -out $CA_DIR/ca.crt -subj "$CA_SUBJ"
# Generate DER format for Pico W (optional, PEM is often easier to embed as string)
openssl x509 -in $CA_DIR/ca.crt -out $CA_DIR/ca.der -outform DER
echo "CA Key: $CA_DIR/ca.key"
echo "CA Cert (PEM): $CA_DIR/ca.crt"
echo "CA Cert (DER): $CA_DIR/ca.der"

echo ""
echo "--- Generating Server Certificate (with IP SAN) ---"
# Generate 2048-bit RSA key for server
openssl genrsa -out $SERVER_DIR/server.key 2048
openssl req -new -key $SERVER_DIR/server.key -out $SERVER_DIR/server.csr -subj "$SERVER_SUBJ"

# Sign the server CSR with the CA and add extensions from the config file
openssl x509 -req -days $DAYS_VALID \
    -in $SERVER_DIR/server.csr \
    -CA $CA_DIR/ca.crt \
    -CAkey $CA_DIR/ca.key \
    -CAcreateserial \
    -out $SERVER_DIR/server.crt \
    -extfile $SERVER_EXT_CONF \
    -extensions v3_server

# Generate DER format for Pico W (optional)
openssl x509 -in $SERVER_DIR/server.crt -out $SERVER_DIR/server.der -outform DER
openssl rsa -in $SERVER_DIR/server.key -out $SERVER_DIR/server_key.der -outform DER # Server private key in DER

echo "Server Key (PEM): $SERVER_DIR/server.key"
echo "Server Cert (PEM): $SERVER_DIR/server.crt"
echo "Server Cert (DER): $SERVER_DIR/server.der"
echo "Server Key (DER): $SERVER_DIR/server_key.der"
echo "Server CSR: $SERVER_DIR/server.csr"
echo "Server Extensions Config: $SERVER_EXT_CONF"

echo ""
echo "--- Generating Client Certificate ---"
# Generate 2048-bit RSA key for client
openssl genrsa -out $CLIENT_DIR/client.key 2048
openssl req -new -key $CLIENT_DIR/client.key -out $CLIENT_DIR/client.csr -subj "$CLIENT_SUBJ"
# Note: Using -CAserial $CA_DIR/ca.srl ensures the same serial file is used if it exists
openssl x509 -req -days $DAYS_VALID -in $CLIENT_DIR/client.csr -CA $CA_DIR/ca.crt -CAkey $CA_DIR/ca.key -CAserial $CA_DIR/ca.srl -out $CLIENT_DIR/client.crt
# Generate DER format for Pico W (optional)
openssl x509 -in $CLIENT_DIR/client.crt -out $CLIENT_DIR/client.der -outform DER
openssl rsa -in $CLIENT_DIR/client.key -out $CLIENT_DIR/client_key.der -outform DER

echo "Client Key (PEM): $CLIENT_DIR/client.key"
echo "Client Cert (PEM): $CLIENT_DIR/client.crt"
echo "Client Cert (DER): $CLIENT_DIR/client.der"
echo "Client Key (DER): $CLIENT_DIR/client_key.der"
echo "Client CSR: $CLIENT_DIR/client.csr"

echo ""
echo "--- Verifying Certificates ---"
echo "Verifying Server Cert against CA:"
openssl verify -CAfile $CA_DIR/ca.crt $SERVER_DIR/server.crt
echo "Verifying Client Cert against CA:"
openssl verify -CAfile $CA_DIR/ca.crt $CLIENT_DIR/client.crt

echo ""
echo "--- Inspecting Server Certificate for SAN ---"
openssl x509 -in $SERVER_DIR/server.crt -noout -text | grep -A 1 "Subject Alternative Name"


echo ""
echo "Done. Certificates generated in: $CERT_BASE_DIR"
echo "Important files for your Server (e.g., Python, Node.js, Nginx):"
echo "  Server Cert: $SERVER_DIR/server.crt (PEM format)"
echo "  Server Key:  $SERVER_DIR/server.key (PEM format)"
echo "  CA Cert:     $CA_DIR/ca.crt (PEM format, needed by server if doing mTLS and verifying client certs)"
echo ""
echo "Important files for Pico W (Client):"
echo "  CA Cert (PEM to embed): $CA_DIR/ca.crt (needed to verify the server)"
echo "  (If doing mTLS) Client Cert (PEM to embed): $CLIENT_DIR/client.crt"
echo "  (If doing mTLS) Client Key (PEM to embed):  $CLIENT_DIR/client.key"
echo ""
echo "To convert PEM certs/keys to C arrays for mbedTLS on Pico W:"
echo "  cat $CA_DIR/ca.crt | xxd -i -name ca_pem_start > ca_cert_pem.h"
echo "  (If mTLS) cat $CLIENT_DIR/client.crt | xxd -i -name client_pem_start > client_cert_pem.h"
echo "  (If mTLS) cat $CLIENT_DIR/client.key | xxd -i -name client_key_pem_start > client_key_pem.h"
echo "  Remember to add a null terminator to the length when parsing PEM strings in mbedTLS!"
echo "  e.g., mbedtls_x509_crt_parse(&cacert, (const unsigned char *)ca_pem_start, ca_pem_start_len + 1);"