====================
 Galera Cluster URL
====================
.. _`Galera Cluster URL`:

The syntax for the Galera Cluster URL address where
the nodes connect to, is shown below::

    <backend schema>://<cluster address>[?option1=value1[&option2=value2]]

where:

- ``<backend schema>`` |---| Refers to the Galera Cluster schema.
  Galera Cluster supports two schemata:
  
    - ``dummy`` |---| This schema is a pass-through backend for
      testing and profiling purposes. The schema does not connect
      to anywhere. Any values given with it will be ignored.
    - ``gcomm`` |---| This schema is a real group communication
      backend used for production purposes. This backend takes
      an address and has a number of parameters that can be set
      throught the option list (or through ``wsrep_provider_options``
      as of version 0.8.x).

- ``<cluster address>`` |---| The cluster adress must be:

    - An address of any current cluster member, if you want to
      connect the node to an existing cluster, or
    - A comma-separated list of possible cluster members. It is
      assumed that the list members can belong to no more than
      one primary component. Or
    - An empty string, if you want this node to be the first in
      a new cluster (that is, there are no pre-existing nodes to
      connect to).

  .. note:: As of version 2.2, Galera Cluster supports a comma-separated
            list of cluster members in the cluster address, such as::

                gcomm://node1,node2:port2,node3?key1=value1&key2=value2...

  .. warning:: Only use an empty ``gcomm://`` address when you want to
               create a *new* cluster. Never use it when your intention
               is to reconnect to an existing one. Furthermore, never
               leave it hard coded in any configuration files.

  .. note:: One way to avoid editing the *my.cnf* configuration file to
            to remove ``gcomm://`` is to start all cluster nodes with the
            following URL::

                gcomm://node1,node2:port2,node3?pc.wait_prim=no&...
                
            The ``pc.wait_prim=no`` option makes the node to wait for a
            primary component indefinitely. Then bootstrap the primary
            component by setting ``pc.bootstrap=1`` on any other node.

- ``options`` |---| The option list can be used to set backend parameters,
  such as the listen address and timeout values. In version .7.x, this was
  the only way to customize the Galera Cluster behavior. The parameter values
  set in the option list are not durable and must be resubmitted on every
  connection to the cluster. As of version 0.8, customized parameters can
  be made durable by seting them in ``wsrep_provider_options``.
  
  The parameters specified in the URL take precedence
  over parameters specified elsewhere (for example ``wsrep_provider_options``
  in the *my.cnf* configuration file).
  
  Parameters that you can set through the option list are
  ``evs.*``, ``pc.*`` and ``gmcast.*``.
  
  The option list can be optionally followed by a list of ``key=value`` *queries*
  according to the URL standard.
  
  .. note:: If the listen address and port are not set in the parameter
            list, ``gcomm`` will listen on all interfaces. The listen
            port will be taken from the cluster address. If it is not
            specified in the cluster address, the default port 4567
            will be used.


.. |---|   unicode:: U+2014 .. EM DASH
   :trim: