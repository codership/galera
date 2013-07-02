================================
 Versioning Information
================================
.. _`Versioning Information`:

*Galera Cluster for MySQL* consists of two software packages:

- Galera wsrep provider
- MySQL server patched with the wsrep
  :abbr:`API (Application Programming Interface)`

This chapter describes the Galera Cluster
release numbering schemes and software packages.

---------------------------
 Release Numbering Schemes
---------------------------

*Galera Cluster* software packages have
their own release numbering schemes as follows:

- *Galera wsrep provider* |---| The Galera wsrep provider release
  numbering scheme is as follows:
  
  ``wsrep API main version.Galera wsrep provider version``
  
  For example, release number 23.2.4 indicates that the Galera
  wsrep provider is meant to be used with wsrep API version
  23.x.x and the Galera wsrep provider version is 2.4.
- *MySQL server patched with the wsrep API* |---| The patched
  MySQL server release numbering scheme is as follows:
  
  ``MySQL version-wsrep API version``
  
  For example, release number 5.5.29-23.7.3 indicates that
  the patched MySQL server release contains MySQL version
  5.5.29 and wsrep API version 23.7.3.

---------------------------
 Software Packages
---------------------------

.. index::
   single: Software packages
   
.. index::
   single: Linux distributions

*Galera Cluster* software packages for different
Linux distributions are available as follows:

- Galera wsrep provider:

    - A 32-bit binary for Debian based distributions
    - A 64-bit binary for Debian based distributions
    - A 32-bit binary for RHEL/CentOS 5
    - A 64-bit binary for RHEL/CentOS 5
    - A 32-bit binary for RHEL/CentOS 6
    - A 64-bit binary for RHEL/CentOS 6
    - A source code package
  
  You can download these packages from: https://launchpad.net/galera/+download.

- MySQL server patched with the wsrep API:

    - A 32-bit binary for Debian based distributions
    - A 64-bit binary for Debian based distributions
    - A 32-bit binary for RHEL/CentOS 5
    - A 64-bit binary for RHEL/CentOS 5
    - A 32-bit binary for RHEL/CentOS 6
    - A 64-bit binary for RHEL/CentOS 6
    - A 32-bit generic tarball for system-wide installations
    - A 64-bit generic tarball for system-wide installations
    - The separate wsrep API patches without the MySQL server

  You can download these packages from: https://launchpad.net/codership-mysql/+download.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim: