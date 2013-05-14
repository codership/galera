================================
 Node Provisioning and Recovery
================================
.. _`Node Provisioning and Recovery`:

The state of new and failed nodes differs from the state of
the primary component and needs to be synchronized. As a result,
new node provisioning and failed node recovery are essentially
the same process of joining the node to the cluster
:abbr:`PC (Primary Component)`.

The initial node state ID is read from the *grastate.txt*
file in ``wsrep_data_dir``, where it is saved every time
the node is gracefully shut down. If the node crashes, its
database state is unknown and its initial Galera node state
is undefined (``00000000-0000-0000-0000-000000000000:-1``).

When a node joins the primary component, it compares its
state ID to that of the :abbr:`PC (Primary Component)` and
if they do not match, the node requests for state transfer
from the cluster.

There are two possibilities to select the state transfer donor:

- Automatic |---| The group communication layer determines
  the state donor from the available members of the primary
  component.
- Manual |---| The state donor name is specified with the
  ``wsrep_sst_donor`` parameter on startup.

In the latter case, if a node with that name is not a part of
the primary component, state transfer fails and the joining node
aborts. Use the same donor name as set in the ``wsrep_node_name``
parameter on the donor node.

.. note:: State transfer is a heavy operation not only on the
          joining node, but also on donor. The state donor may
          not be able to serve client requests. Thus, when possible,
          select the donor manually, based on network proximity.
          Configure the load balancer to transfer client connections
          to the other nodes for the duration of state transfer.

During state transfer the joining node caches writesets received
from other nodes in a *slave queue* and applies them after the
state transfer is over, to catch up with the current primary
component state. Since the state snapshot always has a
state ID, it is easy to determine which writesets are already
contained in the snapshot and should be discarded.

During the catch-up phase, flow control ensures that the slave
queue gets shorter (that is, the cluster replication rate will
be limited to the writeset application rate on the catching node).
However, there is no guarantee on how soon the node will catch up.
When the node catches up, its status becomes ``SYNCED`` and
it will accept client connections.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim: