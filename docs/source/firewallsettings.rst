=============================
 Firewall Settings
=============================
.. _`Firewall Settings`:
.. index::
   pair: iptables; Firewall settings
.. index::
   single: iptables; Ports
.. index::
   single: Firewall settings; Ports


By default, *Galera Cluster* may require all or
some of the following ports to be open between the nodes: 

- 3306 |---| MySQL client connections and *mysqldump* SST
- 4567 |---| *Galera Cluster* replication traffic
- 4568 |---| IST
- 4444 |---| all SSTs besides mysqldump

For example, in a :abbr:`LAN (Local Area Network)` environment
the *iptables* configuration on each node may look as follows::

    # iptables -A INPUT -i eth0 -p tcp -m tcp --source 192.168.0.1/24 --dport 3306 -j ACCEPT
    # iptables -A INPUT -i eth0 -p tcp -m tcp --source 192.168.0.1/24 --dport 4567 -j ACCEPT
    # iptables -A INPUT -i eth0 -p tcp -m tcp --source 192.168.0.1/24 --dport 4568 -j ACCEPT
    # iptables -A INPUT -i eth0 -p tcp -m tcp --source 192.168.0.1/24 --dport 4444 -j ACCEPT 

In a :abbr:`WAN (Wide Area Network)` environment, this setup
may be tedious to manage. Alternatively, with not much loss of
security, you can simply open a full range of ports between
trusted hosts::

    # iptables -A INPUT -p tcp -s 64.57.102.34 -j ACCEPT
    # iptables -A INPUT -p tcp -s 193.166.3.2  -j ACCEPT 

.. note:: The IP addresses in the example are for demonstration purposes only.
          Use the real values from your nodes and netmask in your *iptables*
          configuration.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim: