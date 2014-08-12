=============
 Performance
=============
.. _`Performance`:

.. index::
   pair: Performance; Memory
.. index::
   pair: Performance; Swap size

-----------------------------------------
Write-set Caching during State Transfers
-----------------------------------------
.. _`gcache-during-state-transfers`:

A node under normal operations does not consume much more memory than a regular MySQL server.  The certification index and uncommitted write-sets cause some additional usage, but this is not usually noticeable in typical applications.  Write-set caching during state transfers is the exception.

When a node receives a state transfer, it cannot process or apply incoming write-sets, because it does not at this point have a state to apply them to.  Depending on the state transfer method, (**mysqldump**, for instance), the sending node may also be unable to process or apply write-sets.

The Write-set Cache, (or GCache), caches write-sets on memory-mapped files to disk.  Galera Cluster allocates these files as needed.  Meaning that the limit for the cache is the available disk space.

Writing to disk reduces memory consumption.

.. seealso:: For more information on configuring the Write-set Cache to improve performance, see :ref:`Optional Memory Settings <Optional Memory Settings>`.

-------------------------------------
Customizing the Write-set Cache Size
-------------------------------------
.. _`customizing-gcache-size`:
.. index::
   pair: Configuration Tips; gcache.size
.. index::
   pair: Configuration Tips; wsrep_received_bytes

You can define the size of the write-set cache using the ``gcache.size`` parameter.  The write-set cache should be smaller than the size of the database.

In this context, the database size relates to the method used in state snapshot transfers.  For instance, **rsync** and **xtrabackup** copy the InnoDB log files, while **mysqldump** does not.  As a rule, use the data directory size, including any possible links, less the size of the ring buffer storage file, (called ``galera.cache`` by default).

Another variable to consider in these calculations is the copy speed.  The Incremental State Transfer (IST) method for node provisioning can copy the database five times faster than through **mysqldump** and about 50% faster than **xtrabackup**.  Meaning that your cluster can handle a relatively large write-set cache size.

The database write rate indicates the tail length that the cluster stores in the write-set cache.  You can calculate the write rate using the ``wsrep_received_bytes`` status variable.

1. Determine the size of the write-sets received from other nodes:

   .. code-block:: mysql

      SHOW STATUS LIKE 'wsrep_received_bytes';

      +------------------------+-----------+
      | Variable name          | Value     |
      +------------------------+-----------+
      | wsrep_received_bytes   | 6637093   |
      +------------------------+-----------+

   Note the value and the time, respectively, as ``x1`` and ``t1``.

2. Run the same query again, noting the value and time, respectively, as ``x2`` and ``t2``.

3. Apply these values to the following equation:

   .. code-block:: text

      w = ( x2 - x1 ) / ( t2 - t1 )

The value of ``w`` provides you with the write rate.

.. note:: Consider these configuration tips as guidelines only. For example, in cases where you must avoid state snapshot transfers as much as possible, you may end up using a much larger write-set cache than suggested above.

-----------------------------------
Setting Parallel Slave Threads
-----------------------------------
.. _`parallel-slave-threads`:
.. index::
   pair: Configuration Tips; innodb_autoinc_lock_mode
.. index::
   pair: Configuration Tips; innodb_locks_unsafe_for_binlog
.. index::
   pair: Configuration Tips, wsrep_slave_threads

There is no rule about how many slave threads you need for replication.  Parallel threads do not guarantee better performance.  But, parallel applying does not impair regular operation performance and may speed up the synchronization of new nodes with the cluster.

You should start with four slave threads per CPU core:

.. code-block:: ini

   wsrep_slave_threads=4

The logic here is that, in a balanced system, four slave threads can typically saturate a CPU core.  However, I/O performance can increase this figure several times over.  For example, a single-core ThinkPad R51 with a 4200 RPM drive can use thirty-two slave threads.

Parallel applying requires the following settings:

.. code-block:: ini

   innodb_autoinc_lockmode=2
   innodb_locks_unsafe_For_binlog=1

You can use the ``wsrep_cert_deps_distance`` status variable to determine the maximum number of slave threads possible.  For example:

.. code-block:: mysql

   SHOW STATUS LIKE 'wsrep_cert_deps_distance';

   +----------------------------+-----------+
   | Variable name              | Value     |
   +----------------------------+-----------+
   | wsrep_cert_deps_distance   | 23.88889  |
   +----------------------------+-----------+

This value essentially determines the number of write-sets that the node can apply in parallel on average.  

.. warning:: Do not use a value for ``wsrep_slave_threads`` that is higher than the average given by the ``wsrep_cert_deps_distance`` status variable.


------------------------------------
 Dealing with Large Transactions
------------------------------------
.. _`large-transactions`:

Large transactions, for instance the transaction caused by a ``DELETE`` query that removes millions of rows from a table at once, can lead to diminished performance.  If you find that you must perform frequently transactions of this scale, consider using **pt-archiver** from the Percona Toolkit.

For example, if you want to delete expired tokens from their table on the ``keystone`` database at ``dbhost``, you might run something like this:

.. code-block:: console

   $ pt-archiver --source h=dbhost,D=keystone,t=token \
      --purge --where "expires < NOW()" --primary-key-only \
      --sleep-coef 1.0 --txn-size 500

This allows you to delete rows efficiently from the cluster.

.. seealso:: For more information on **pt-archiver**, its syntax and what else it can do, see the `manpage <http://www.percona.com/doc/percona-toolkit/2.1/pt-archiver.html>`_.

