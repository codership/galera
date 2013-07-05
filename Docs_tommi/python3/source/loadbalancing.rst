================
 Load Balancing
================
.. _`Load Balancing`:
.. index::
   single: Load balancing

*Galera Cluster* guarantees node consistency regardless
of where and when the query is issued. In other words, you are
free to choose a load-balancing approach that best suits your
purposes. If you decide to place the load balancing mechanism
between the database and the application, you can consider, for
example, the following tools:

- *HAProxy* - HAProxy is an open source TCP/HTTP load balancer.
- *Pen* - *Pen* is another open source TCP/HTTP load balancer.
  Pen performs better than HAProxy on SQL traffic.
- *Galera Load Balancer* - *Galera Load Balancer* was inspired
  by Pen, but is limited to balancing generic TCP connections only.