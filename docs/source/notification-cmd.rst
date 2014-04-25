==============================
 Notification Script
==============================

If ``wsrep_notify_cmd`` is set, the server will try to invoke this command every time cluster membership or local node status changes. This can be used to (re)configure load balancers, raise alarm and so on.

------------------------------
Notification Command Arguments
------------------------------

The command will be passed one or more of the following options:

- ``--status [status]`` indicates the status of the node.

- ``--uuid [state UUID]`` indicates the cluster state UUID.

- ``--primary [yes|no]`` indicates whether the current cluster component is primary or not.

- ``--members [member UUID list]``

- ``--index [n]`` indicates the index of this node in the member list, (base 0).


--------------------------------------
Node Status String
--------------------------------------

- ``Undefined`` the node just started and is not connected to any primary component.

- ``Joiner`` the node is connected to primary component and now is receiving state snapshot.

- ``Donor`` the node is connected to primary component and now is sending state snapshot.

- ``Joined`` the node has complete state and now is catching up with the cluster.

- ``Synced`` the node has synchronized with the cluster.

- ``Error([error code if available])``

Only the node in ``Synced`` state should accept connections. See galera_node_fsm for additional information on node states.

--------------------------------------
Member List Element
--------------------------------------

Each member list element has the following a format::

node UUID / node name / incoming address

- ``node UUID`` Refers to an unique node ID automatically assigned to it by wsrep Provider.

- ``node name`` Refers to the name of the node as set in ``wsrep_node_name``.

- ``incoming address`` Refers to the address for client connections as set in ``wsrep_node_incoming_address``

