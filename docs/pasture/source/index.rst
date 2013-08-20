.. Galera documentation master file, created by
   sphinx-quickstart on Tue Oct  9 23:50:34 2012.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Galera Cluster for MySQL Reference Manual
=========================================

`Galera Cluster for MySQL <http://www.codership.com/content/using-galera-cluster>`_ 
is a synchronous multi-master database cluster, based on Oracle's MySQL/InnoDB.
The MySQL forks from Percona and MariaDB also provide Galera clustered versions 
as `Percona XtraDB Cluster <http://www.percona.com/software/percona-xtradb-cluster>`_
and `MariaDB Galera Cluster <https://downloads.mariadb.org/mariadb-galera/>`_
respectively. [#fn_trademarks]_ Galera is 100% GPL open source technology and is 
integrated with MySQL Server by Codership Oy. 

To read more about Galera, you can continue reading :doc:`about_galera_cluster`.
If you just want to quickly install and start your first cluster, you should 
jump to :doc:`howto/start_your_first_galera_cluster`.

This is the official documentation for *Galera Cluster for MySQL* and covers
both Codership's *Galera Cluster for MySQL*, as well as *Percona XtraDB Cluster* 
and *MariaDB Galera Cluster*. Where Percona or MariaDB differ or contain 
additional features and functionality, this is specifically highlighted througout 
the manual.

This manual was first released in conjunction with Galera 3.0 in 2013, however 
we have made an attempt to cover feature compatibility back to Galera 1.0. 

In other words, you should be able to use this reference manual for all Galera 
versions and together with all major MySQL variants (forks).

.. _other_mysql_manuals:

Other (upstream) MySQL manuals
------------------------------

This manual only covers the Galera replication library and the "Write Set 
Replication (wsrep) patches" added to mysqld itself. Standard MySQL features
are only covered if relevant to using Galera, such as how to use certain 
configuration options or how to mix Galera clustering and classic MySQL 
replication.

Other standard MySQL functionality, if it is not related to Galera, is not 
covered by this manual, rather the reader should refer to the following official 
documentations for each product:

* `MySQL Reference Manual <http://dev.mysql.com/doc/refman/5.5/en/index.html>`_
* `Percona Server Documentation <http://www.percona.com/doc/percona-server/5.5/?id=percona-server:start>`_
* `MariaDB AskMonty Knowledgebase <https://kb.askmonty.org/en/mariadb/>`_


.. todo: Below is the full table of contents. I will limit it to show only top level headings, but for now it is the outline we have.

.. _contents:

Contents
--------



.. toctree::
   :numbered:

   about_galera_cluster
   quickstart_and_other_howtos
   installing
   configuration_and_monitoring
   startup_and_administration
   understanding_galera
   different_galera_architectures
   load_balancers
   mixing_galera_with_mysql_replication
   migrating_to_galera
   incompatibilities_with_standard_mysql
   faq
   legal_and_meta_information
   external_resources
   release_notes  


.. _indices_and_tables: 

Indices and tables
------------------

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`


.. rubric:: Footnotes

.. [#fn_trademarks] *"MySQL"* is a registered trademark of Oracle Corporation. 
                    *"Percona XtraDB Cluster"* and *"Percona Server"* are 
                    registered trademarks of Percona LLC. *"MariaDB"* and 
                    *"MariaDB Galera Cluster"* are registered trademarks of 
                    Monty Program Ab. See :doc:`legal_and_meta_information` 
                    for more information.