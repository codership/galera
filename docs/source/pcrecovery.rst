=================================
Recovering the Primary Component
=================================
.. _`recovering-pc`:
.. index::
   pair: Configuration; pc.recovery


Cluster nodes can store the Primary Component state to disk.  The node records the state of the Primary Component and the UUID's of the nodes connected to it.  In the event of an outage, once all nodes that were part of the last saved state achieve connectivity, the cluster recovers the Primary Component.

This feature allows for:

- Automatic recovery from full cluster crashes, such as a data center power outage.

- Graceful full cluster restarts, without the need for explicitly bootstrapping a new Primary Component.

In the event that the write-set position differs between the nodes, the recovery process also requires a full state snapshot transfer.

.. seealso:: For more information on this feature, see the :ref:`pc.recovery <pc.recovery>` parameter.  By default, it is enabled starting in version 3.6.



------------------------------------------
Understanding the Primary Component State
------------------------------------------
.. _`understand-pc-state`:

When a node stores the Primary Component state to disk, it saves it as the ``gvwstate.dat`` file.  The node creates and updates this file when the cluster forms or changes the Primary Component.  This ensures that the node retains the latest Primary Component state that it was in.  If the node loses connectivity, it has the file to reference.  If the node shuts down gracefully, it deletes the file.

.. code-block:: plain

   my_uuid: d3124bc8-1605-11e4-aa3d-ab44303c044a
   #vwbeg
   view_id: 3 0dae1307-1606-11e4-aa94-5255b1455aa0 12
   bootstrap: 0
   member: 0dae1307-1606-11e4-aa94-5255b1455aa0 1
   member: 47bbe2e2-1606-11e4-8593-2a6d8335bc79 1
   member: d3124bc8-1605-11e4-aa3d-ab44303c044a 1
   #vwend

The ``gvwstate.dat`` file breaks into two parts:

- **Node Information** Provides the node's UUID, in the ``my_uuid`` field.

- **View Information**  Provides information on the node's view of the Primary Component, contained between the ``#vwbeg`` and ``#vwend`` tags.

  - ``view_id`` Forms an identifier for the view from three parts:

    - *view_type* Always gives a value of ``3`` to indicate the primary view.
    - *view_uuid* and *view_seq* together form a unique value for the identifier.

  - ``bootstrap`` Displays whether or not the node is bootstrapped, but does not effect the Primary Component recovery process.

  - ``member`` Displays the UUID's of nodes in this primary component.


-------------------------------------------
Modifying the Saved Primary Component State
-------------------------------------------
.. _`modifying-pc-state`:


In the event that you find yourself in the unusual situation where you need to force certain nodes to join each other specifically, you can do so by manually changing the saved Primary Component state.

.. warning:: Under normal circumstances, for safety reasons, you should entirely avoid editing or otherwise modifying the ``gvwstate.dat`` file.  Doing so may lead to unexpected results.

When a node starts for the first time or after a graceful shutdown, it randomly generates and assigns to itself a UUID, which serves as its identifier to the rest of the cluster.  If the node finds a ``gvwstate.dat`` file in the data directory, it reads the ``my_uuid`` field to find the value it should use.

By manually assigning arbitrary UUID values to the respective fields on each node, you force them to join each other, forming a new Primary Component, as they start.

For example, assume that you have three nodes that you would like to start together to form a new Primary Component for the cluster.  You will need to generate three UUID values, one for each node.

.. code-block:: mysql

   SELECT UUID();

   +--------------------------------------+
   | UUID()                               |
   +--------------------------------------+
   | 47bbe2e2-1606-11e4-8593-2a6d8335bc79 |
   +--------------------------------------+

You would then take these values and use them to modify the ``gwstate.dat`` file on ``node1``:

.. code-block:: plain

   my_uuid: d3124bc8-1605-11e4-aa3d-ab44303c044a
   #vwbeg
   view_id: 3 0dae1307-1606-11e4-aa94-5255b1455aa0 12
   bootstrap: 0
   member: 0dae1307-1606-11e4-aa94-5255b1455aa0 1
   member: 47bbe2e2-1606-11e4-8593-2a6d8335bc79 1
   member: d3124bc8-1605-11e4-aa3d-ab44303c044a 1
   #vwend

Then repeat the process for ``node2``:

.. code-block:: plain

   my_uuid: 47bbe2e2-1606-11e4-8593-2a6d8335bc79
   #vwbeg
   view_id: 3 0dae1307-1606-11e4-aa94-5255b1455aa0 12
   bootstrap: 0
   member: 0dae1307-1606-11e4-aa94-5255b1455aa0 1
   member: 47bbe2e2-1606-11e4-8593-2a6d8335bc79 1
   member: d3124bc8-1605-11e4-aa3d-ab44303c044a 1
   #vwend

And, the same again for ``node3``:

.. code-block:: plain

   my_uuid: d3124bc8-1605-11e4-aa3d-ab44303c044a
   #vwbeg
   view_id: 3 0dae1307-1606-11e4-aa94-5255b1455aa0 12
   bootstrap: 0
   member: 0dae1307-1606-11e4-aa94-5255b1455aa0 1
   member: 47bbe2e2-1606-11e4-8593-2a6d8335bc79 1
   member: d3124bc8-1605-11e4-aa3d-ab44303c044a 1
   #vwend


Then start all three nodes without the bootstrap flag.  When they start, Galera Cluster reads the ``gvwstate.dat`` file for each.  It pulls its UUID from the file and uses those of the ``member`` field to determine which nodes it should join in order to form a new Primary Component.

