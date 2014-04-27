
==========================
Scriptable State Snapshot Transfers
==========================
.. _`Scriptable SST`:

Galera Cluster has an interface to customize state snapshot transfer through an external script. The script assumes that the storage engine initialization on the receiving node takes place only after the state transfer is complete. In short, this transfer copies the contents of the source data directory to the destination data directory (with possible
variations).

As of wsrep API patch level 23.7, SST parameters are named. Individual scripts can use the ``wsrep_sst_common.sh`` file, which contains common functions for parsing argument lists, logging errors, and so on. There is no constraint on the order or number of parameters. New parameters can be added and any parameter can be ignored by the script. 

---------------------------
Common Parameters
---------------------------
.. _`Common Parameters`:

These parameters are always passed to any state transfer script:

- ``role``
- ``address``
- ``auth``
- ``datadir``
- ``defaults-file``
- ``parent``

^^^^^^^^^^^^^^^^^^^^^^^^^^
Donor-specific Parameters
^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Donor Parameters`:

These parameters are passed to the state transfer script by the state transfer process:

- ``socket`` The local server (donor) socket for communications, if is required.

- ``gtid`` The :term:`Global Transaction ID` in format: ``<uuid>:<seqno>``.

- ``bypass`` This parameter specifies whether the actual data transfer should be skipped and only the GTID should be passed to the receiving server (to go straight to Incremental State Transfer).

^^^^^^^^^^^^^^^^^^^^^^^^^^
``mysqldump``-specific Parameters
^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`mysqldump parameters`:

These parameters are only passed to the ``wsrep_sst_mysqldump``:

- ``user`` The MySQL user to connect to both remote and local servers. The user must be the same on both servers.

- ``password`` MySQL user password.

- ``host`` The remote server (receiver) host address.

- ``port`` The remote server (receiver) port.

- ``local-port`` The local server (donor) port.



----------------------------
Calling Conventions
----------------------------
.. _`Calling Conventions`:

Scripts for state snapshot transfers should adhere to the following conventions.

^^^^^^^^^^^^^^^^^^^^^^^^^^^
Receiver
^^^^^^^^^^^^^^^^^^^^^^^^^^^

On receiving side the script should accept the following positional arguments:

- Role. This will be 'joiner'.

- Address to receive snapshot at. It will be either the value of `wsrep_sst_receive_address`` or some sensible default in the ``<ip>:<port>`` shape if the former is not set.

- Authentication information as set in ``wsrep_sst_auth``.

- The value of ``mysql_real_data_home`` (MySQL data directory path).

- Path to the configuration file, (``my.cnf`` or ``my.ini``, depending on your build).

After script prepares the node for accepting state snapshot (e.g. ``wsrep_sst_rsync`` starts ``rsync`` in server mode), the script should print the following string to standard output::

	ready <address>

For ``<address>``, use the real address at which the node is waiting for the state transfer.  This address, the value of ``wsrep_sst_auth``, and the name of the script will be passed in a state transfer request to sender and will be part of input to sender script.

When the state transfer is over the script should print to standard output the global transaction ID of the received state::

	e2c9a15e-5485-11e0-0800-6bbb637e7211:8823450456

and exit with a ``0`` exit status.

^^^^^^^^^^^^^^^^^^^^^^^^^^^
Sender
^^^^^^^^^^^^^^^^^^^^^^^^^^^

On the sending side the script should accept the following positional arguments:

- Role. This will be 'donor'.

- Address to send snapshot to. It will be the value received in state transfer request.

- Authentication information as received in state transfer request.

- The value of ``mysql_real_data_home`` (MySQL data directory path) on this node.

- Path to the configuration file, (``my.cnf`` or ``my.ini``, depending on your build).

- state UUID.

- seqno of the last committed transaction.

The script is run in a total order isolation which guarantees that no more commits will happen until the script exits or prints ``continue\n`` to the standard output.

The following signals from the script are accepted:

- ``flush tables\n`` (Optional) Asks the server to flush tables. When done the server will create tables_flushed file in the data directory.

- ``continue\n`` (Optional) Tells the server that it can continue committing.

- ``done\n`` (Mandatory) Tell the server that state transfer has completed successfully. Mandatory. The script then should exit with a ``0`` code.

In case of failure the script is expected to return a code that most closely corresponds to the error encountered. This will be returned to receiver through group communication and receiver will leave the cluster and abort (since its data directory is now in inconsistent state).




.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
   



