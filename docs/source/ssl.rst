=============
 Enabling SSL
=============
.. _`enabling-ssl`:
.. index::
   pair: Parameters; socket.ssl_compression
.. index::
   pair: Parameters; socket.ssl_cipher
.. index::
   pair: Parameters; socket.ssl_cert
.. index::
   pair: Parameters; socket.ssl_key
   
   
For the encryption of replication traffic, Galera Cluster supports :abbr:`SSL (Secure Sockets Layer)`.  It does not support authentication.  SSL is a cluster-wide option.  You must enable it for all nodes in the cluster or none at all.

.. warning:: Galera Cluster SLL support only covers Galera Cluster communications.  State Snapshot Transfers happen outside of Galera Cluster, so you must protect them separately.  For example, consider using the internal SSL support of the MySQL client or the **stunnel** program to protect **rsync** traffic.

To implement SSL on your cluster, complete the following steps:

1. Generate a private certificate/key pair for the cluster.  For instance, using **openssl** run the following command:

   .. code-block:: console
   
      $ openssl req -new -x509 -days 365000 -nodes \
         -keyout key.pem -out cert.pem

   .. note:: When the certificate expires, there is no way to update the cluster without a complete shutdown.  Use a large value for the ``-days`` parameter.

2. Use a secure channel to copy the certificate/key pair files into the ``/etc/mysql/`` directory on each node in the cluster.

3. On each node, update the configuration file, (``my.cnf`` or ``my.ini``, depending on your build), to include the certificate/key pair.

   .. code-block:: ini
   
      socket.ssl_cert = /path/to/cert.pem
      socket.ssl_key = /path/to/key.pem

Once all of the nodes have the update, Galera Cluster will use SSL to encrypt communication between the nodes.

.. seealso:: For information on other parameters for SSL, see :ref:`socket.ssl_compression <socket.ssl_compression>` and :ref:`socket.ssl_cipher <socket.ssl_cipher>`.



