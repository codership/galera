Certificates
============

Note: These certificates should be used for testing purposes only.

Certificate and key files in this directory:

Simple key and certificate
--------------------------

This key and certificate must be identical on each node:
- galera_key.pem/galera_cert.pem - standalone key/certificate which
  can be used for testing in symmetric setups


Certificate Chain
-----------------

Keys and certificates below have been created by using easy-rsa 3 CLI
utility: https://github.com/OpenVPN/easy-rsa

- galera-ca.pem - Root CA certificate for testing
- galera-int.pem - Intermediate certificate for testing
- galera-server-n.key Server private key for node n
- galera-server-n.pem - Server certificate for node n
- bundle-galera-server-n.pem File containing both server and intermediate
                             certificate for node n

See script gen-cert-chain.sh in this same directory for chain generation.