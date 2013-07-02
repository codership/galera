=============
 Performance
=============
.. _`Performance`:

.. index::
   pair: Performance; Memory
.. index::
   pair: Performance; Swap size

In normal operation, a *Galera Cluster* node does not consume
much more memory than a regular MySQL server. Additional
memory is consumed for the certification index and uncommitted
write sets, but usually this is not noticeable in a typical
application. However, writeset caching during state transfer
makes an exception.

When a node is receiving a state transfer, it cannot process
and apply incoming write sets because it has no state to
apply them to yet. Depending on a state transfer mechanism
(for example, *mysqldump*), the node that sends the state
transfer may not be able to apply write sets. Instead, the
node must cache the write sets for a catch-up phase. The
Writeset Cache (GCache) is used to cache write sets on
memory-mapped files on disk. These files are allocated as
needed. In other words, the limit for the cache is the
available disk space. Writing on disk reduces memory
consumption.

.. seealso:: Chapter :ref:`Optional Memory Settings <Optional Memory Settings>`

------------------------------------
 Dealing with Large Transactions
------------------------------------
.. _`Dealing with Large Transactions`:

If you must frequently handle large transactions, such as transactions
caused by the ``DELETE`` command that may delete millions of rows from
a table at once, we recommend using the Percona toolkit's *pt-archiver*
command. For example:

  ``pt-archiver --source h=dbhost,D=keystone,t=token --purge --where "expires < NOW()" --primary-key-only --sleep-coef 1.0 --txn-size 500``

This tool deletes rows efficiently. For more information on the tool,
see: http://www.percona.com/doc/percona-toolkit/2.1/pt-archiver.html.