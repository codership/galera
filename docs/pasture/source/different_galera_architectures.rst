Different Galera Architectures
==============================

In this chapter we present some commonly used system architectures for Galera
Cluster. From the :doc:`understanding_galera` chapter you should already be 
familiar with the concepts of *primary component*, *quorum* and resulting need
for at minimum 3 nodes. (However, we also consider the special case when you only
have 2 nodes available, and your options at dealing with that.)

Most commonly Galera is used with some load balancer. Often the load balancer
functionality can even be built into your MySQL client library - this is 
currently the case with the official JDBC and PHP MySQL clients. However, we
also cover some architectures where a load balancer is not used.

Galera has been found to perform very well also when used over Wider Area 
Networks, even across contintents. Using Galera over WAN is also covered here.

.seealso:: :doc:`load_balancers`

TODO: subsections (toctree)