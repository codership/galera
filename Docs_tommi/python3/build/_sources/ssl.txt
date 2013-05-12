=============
 Enabling SSL
=============
.. _`Enabling SSL`:

Galera library supports :abbr:`SSL (Secure Sockets Layer)`
for the encryption of replication traffic. SSL is a cluster-wide
option and must be enabled either on all of the nodes or none
at all. 

To use SSL, you must generate a private certificate/key pair
for the cluster, for example, by the following command::

    $ openssl req -new -x509 -days 365000 -nodes -keyout key.pem -out cert.pem

.. note:: It is crucial to generate a certificate/key pair, which is valid
          for a long time. When the certificate expires, there will be no
          way to update the cluster without complete shutdown. Thus, use a
          large value for the the ``-days`` parameter.

Copy this certificate/key pair to all of the nodes and take it to use
by specifying the following Galera options::

    socket.ssl_cert = <path_to_cert_file>; socket.ssl_key = <path_to_key_file>

Other SSL configuration parameters include ``socket.ssl_compression`` and
``socket.ssl_cipher``. See :ref:`Galera Parameters <Galera Parameters>`
for details.

.. warning:: Galera SSL support only covers Galera communication. Since state
             snapshot transfer happens outside of Galera, protect it separately.
             You can use, for example, the internal SSL support in the MySQL
             client or the **stunnel** program to protect *rsync* traffic.