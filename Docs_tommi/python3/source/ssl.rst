=============
 Enabling SSL
=============
.. _`Enabling SSL`:

.. index::
   pair: Parameters; socket.ssl_compression

.. index::
   pair: Parameters; socket.ssl_cipher

.. index::
   pair: Parameters; socket.ssl_cert

.. index::
   pair: Parameters; socket.ssl_key
   
*Galera Cluster* supports :abbr:`SSL (Secure Sockets Layer)`
for the encryption of replication traffic. Authentication
is not supported. SSL is a cluster-wide option and must be
enabled either on all of the nodes or none at all. 

To use SSL, you must generate a private certificate/key pair
for the cluster, for example, by the following command::

    $ openssl req -new -x509 -days 365000 -nodes -keyout key.pem -out cert.pem

.. note:: It is crucial to generate a certificate/key pair, which is valid
          for a long time. When the certificate expires, there will be no
          way to update the cluster without complete shutdown. Thus, use a
          large value for the the ``-days`` parameter.

Copy this certificate/key pair to the */etc/mysql* directory on all of the
nodes. Copy the files over a secure channel between the nodes.

Take the certificate/key pair to use by specifying the
following *Galera Cluster* options::

    socket.ssl_cert = <path_to_cert_file>; socket.ssl_key = <path_to_key_file>

Other SSL configuration parameters include ``socket.ssl_compression`` and
``socket.ssl_cipher``. See :ref:`Galera Parameters <Galera Parameters>`
for details.

.. note:: You cannot use a mixed cluster where some nodes have SSL and
          some do not. We recommend configuring SSL when you are setting
          up a new cluster. If you must add SSL support on a production
          system, you must rebootstrap the cluster and accept a brief
          outage.

.. warning:: The *Galera Cluster* SSL support only
             covers *Galera Cluster* communication. Since state
             snapshot transfer happens outside of *Galera Cluster*,
             protect it separately.
             You can use, for example, the internal SSL support in the MySQL
             client or the **stunnel** program to protect *rsync* traffic.