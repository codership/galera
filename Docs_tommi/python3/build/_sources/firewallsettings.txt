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


Galera uses *iptables* as the firewall between database nodes.
In short, the *iptables* utility controls the network packet
filtering code in the Linux kernel. 

By default a Galera node requires ports 3306 and 4567 to be
open for connections from the other nodes in the
:abbr:`LAN (Local Area Network)`. See below for an example
*iptables* configuration::

    # iptables -A INPUT -i eth0 -p tcp -m tcp --source 192.168.0.1/24 --dport 3306 -j ACCEPT
    # iptables -A INPUT -i eth0 -p tcp -m tcp --source 192.168.0.1/24 --dport 4567 -j ACCEPT

.. note:: The IP addresses in the example are for demonstration purposes only.
          Use the real values from your nodes and netmask in your *iptables*
          configuration.
