================================
 Installing Galera on EC2
================================
.. _`Installing Galera on EC2`:

A company called Severalnines produces ClusterControl, a tool
to easily deploy, monitor and manage MySQL clusters. With the
Severalnines ClusterControl you can deploy a *Galera Cluster*
with a single command. ClusterControl will establish SSH
connections to each node, install and start the necessary
software.

Proceed as follows:

1. Allow all incoming ICMP and TCP packets from other
   nodes in the same security group.

   Each Galera node must be accessible at several ports.
   The default EC2 security group allows this. Allow also
   inbound HTTP and SSSH access at least to the first node,
   which we will call *Control Instance* below.

2. Start the nodes. Start four or more of your favourite EC2
   instances. In this example, we use 
   *RightImage_CentOS_5.6_x64_v5.7.14*. Select one of the instances
   to run as a control. The other instances will be identical
   *Galera Cluster* nodes.

3. Set up a keypair. Copy the keypair used to start the instances
   to the control instance. The deployment script uses the keypair
   to install and configure the rest of the nodes::

       scp -i keypair keypair root@<control_instance_public_dns_name>:

4. Generate the deployment package. Open a browser and go to
   http://www.severalnines.com/galera-configurator/. Proceed as follows:
   
       - On page 1, General Settings, specify the cloud provider
	     (Amazon EC2), the number of instances, mysql root password,
		 and OS user (root)
       - On page 2, Storage, specify the EC2 instance RAM size, the
	     planned database size, and the number of cores.
       - On page 3, Individual Nodes:
	   
	       - For the ClusterControl server, specify the local IP
                 address and the absolute path to the keypair set up
                 in step 3 (in this case **/root/keypair**).
               - For *Galera Cluster* servers, specify the local IP
                 addresses and the desirable location of the data directory
                 (RightScale images mount the local storage under **/mnt**).

   When you have entered the required information, you will be sent a
   tarball contaning the deployment scripts and a personal HTTP link
   at the Severalnines site.
5. Deploy the cluster from the control instance as follows
   (Note that you will get a different tarball link)::

       [root@<control instance> ~]# wget http://www.severalnines.com/galera-configurator/tmp/mv00aikh65qmoju9p1qqlfaoo6/s9s-galera-1.0.0-rpm.tar.gz
       [root@<control instance> ~]# tar -xzf s9s-galera-1.0.0-rpm.tar.gz 
       [root@<control instance> ~]# cd ./s9s-galera-1.0.0-rpm/mysql/scripts/install
       [root@<control instance> install]# ./deploy.sh

   Answer the installation questions and wait until ClusterControl
   connects to each EC2 instance, installs *Galera Cluster* and
   starts it. 
6. A successful installation is indicated by an invitation
   to visit http://<control_instance_public_dns_name>/cmon/ to check
   the cluster. 

.. note:: Set up a HTTP password to protect the monitor from unauthorized access.
