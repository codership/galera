==========
 Glossary
==========
.. _`Glossary`:

.. glossary::
   :sorted:

   Galera Replication
      Galera Replication is a general purpose replication plugin for any 
      transactional system. It can be used to create a synchronous multi-master
      replication solution to achieve high availability and scale-out.
      
      See :ref:`Galera Replication <Galera Replication>` for more details.

   Global Transaction ID
      To keep the state identical on all nodes, the wsrep API uses
      global transaction IDs (GTID), which are used to both:

        - Identify the state change
        - Identify the state itself by the ID of the last state change

        The GTID consists of:

        - A state UUID, which uniquely identifies the state and the
          sequence of changes it undergoes
        - An ordinal sequence number (seqno, a 64-bit signed integer)
          to denote the position of the change in the sequence
          
      See :ref:`wsrep API <wsrep API>` for more details.

   Primary Component
    In addition to single node failures, the cluster may be split into
    several components due to network failure. 
    In such a situation, only one of the components can continue to
    modify the database state to avoid history divergence. This component
    is called the Primary Component (PC). 
      
        See :ref:`Primary Component <Primary Component>` for more details.
   
   wsrep API
      The wsrep API is a generic replication plugin interface for databases.
      The API defines a set of application callbacks and replication plugin calls.
      
      See :ref:`wsrep API <wsrep API>` for more details.

   State Snapshot Transfer (SST)
      State Snapshot Transfer refers to a full data copy from one cluster
      node (donor) to the joining node (joiner). See also the
      definition for Incremental State Transfer (IST).
      
      See :ref:`State Snapshot Transfer (SST) <State Snapshot Transfer (SST)>` for more details.

   Incremental State Transfer (IST)
      In incremental state transfer, a node only receives the missing write
      sets and catch up with the group by replaying them. See also the
      definition for State Snapshot Transfer (SST).
      
      See :ref:`Incremental State Transfer (IST) <Incremental State Transfer (IST)>` for more details.
      
    Writeset Cache (GCache)
      Galera stores write sets in a special cache called Writeset Cache (GCache).
      In short, GCache is a memory allocator for write sets and its primary purpose
      is to minimize the write set footprint on the RAM.
      
      See :ref:`Writeset Cache (GCache) <Writeset Cache (GCache)>` for more details.

    Galera Arbitrator
      If the expense of adding, for example, a third datacenter is too high,
      you can use the Galera arbitrator. An arbitrator is a member of the
      cluster which participates in voting, but not in actual replication.
      
      See :ref:`Galera Arbitrator <Galera Arbitrator>` for more details.
