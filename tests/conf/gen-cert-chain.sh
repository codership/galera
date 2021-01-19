#!/usr/bin/env bash
#
# Copyright (C) 2021 Codership Oy <info@codership.com>
#
# Helper script to generate certificate chains for testing.
#
set -o errexit -o errtrace -o nounset
set -o xtrace

export EASYRSA_REQ_ORG="Codership Oy"
export EASYRSA_REQ_EMAIL="devel@galeracluster.com"
export EASYRSA_REQ_OU="Galera Devel"
export EASYRSA_REQ_CITY="Helsinki"
export EASYRSA_REQ_PROVINCE="Uusimaa"
export EASYRSA_REQ_COUNTRY="FI"
export EASYRSA_DN=org
export EASYRSA_BATCH=yes

# Init pki directory for root CA
easyrsa --pki-dir=rootCA init-pki
# Create root CA in pki/ca.crt
easyrsa --pki-dir=rootCA --req-cn="Galera Root CA" build-ca nopass

# Initialize pki directory for intermediate CA
easyrsa --pki-dir=intCA init-pki
easyrsa --pki-dir=intCA build-ca nopass subca
# Create request for intermediate CA
easyrsa --pki-dir=intCA --req-cn="Galera Int" gen-req ca nopass

mkdir -p intCA/x509-types
# Write custom server type for server certificates without
# extendedKeyUsage.
cat <<EOF > intCA/x509-types/server
basicConstraints = CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer:always
# extendedKeyUsage = serverAuth
keyUsage = digitalSignature,keyEncipherment
EOF

# Copy request under rootCA
cp intCA/reqs/ca.req rootCA/reqs/galera-int.req
# Create intermediate CA in pki/issued/galear-int.crt
easyrsa --pki-dir=rootCA sign-req ca galera-int
# Copy generated intermediate CA under intCA
cp rootCA/issued/galera-int.crt intCA/ca.crt
# Create server certificates using intermediate CA
for i in galera-server-1 galera-server-2 galera-server-3
do
    easyrsa --pki-dir=intCA --req-cn=$i gen-req $i nopass
    easyrsa --pki-dir=intCA sign-req server $i
done

cp rootCA/ca.crt galera-ca.pem
cp intCA/ca.crt galera-int.pem
find intCA/issued -name "*.crt" -exec sh -c 'x="{}"; cp "$x" "$(basename $x .crt).pem"' \;
find intCA/private -name "*.key" -exec cp {} . \;

# Validate generated certificates
openssl verify -CAfile galera-ca.pem galera-int.pem
for i in galera-server-1.pem galera-server-2.pem galera-server-3.pem
do
    openssl verify -CAfile galera-ca.pem -untrusted galera-int.pem $i
    openssl x509 -in $i > bundle-$i
    openssl x509 -in galera-int.pem >> bundle-$i
    openssl crl2pkcs7 -nocrl -certfile bundle-$i | openssl pkcs7 -print_certs -noout
done

