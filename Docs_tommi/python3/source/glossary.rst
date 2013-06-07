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