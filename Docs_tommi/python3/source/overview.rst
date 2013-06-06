.. galera documentation master file, created by
   sphinx-quickstart on Sat Apr 20 12:00:55 2013.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

=========
 Overview
=========

*Galera Cluster for MySQL* is a synchronous multi-master
database cluster, based on Oracle's MySQL/InnoDB. When you
use *Galera Cluster for MySQL*, you can read from and write
to any node, and you can lose any individual node without
interruption in operations, and without the need to handle
complicated failover procedures.

On a high level, *Galera Cluster for MySQL* consists of the
standard MySQL server and the standard InnoDB storage engine,
using the :term:`Galera Replication` plugin for replication.
To be more specific, we do extend the MySQL replication plugin
API to provide all the information and hooks required for true
multi-master, synchronous replication. We call this API the
*Write Set REPlication API*.

*Galera Cluster for MySQL* provides *certification based*
replication. The idea is that the transactions to be replicated
|---| the write set |---| not only contains the database rows
to replicate, but also includes information on all the locks
that were held by the database during the transaction. Each
node *certifies* the replicated write set against other write
sets in the applier queue, and if there are no conflicting
locks, the write set can be applied. At this point, the transaction
is considered committed, after which each node continues to
apply it to the tablespace. 

This approach is also called *virtually synchronous replication*
as it is  logically synchronous, but actual writing and committing
to the tablespace happens independently and, thus, asynchronously
on each node.

Contents:

.. toctree::

   benefits
   glossary
   versioninginformation
   thirdpartygaleraimplementations

.. |---|   unicode:: U+2014 .. EM DASH
   :trim: