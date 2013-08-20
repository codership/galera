HowTo: Deploy a Galera Cluster into EC2 using Severalnines ClusterControl
=========================================================================

Severalnines produces ClusterControl, a tool to easily deploy, monitor and manage MySQL clusters. With Severalnines ClusterControl you can deploy a full Galera Cluster with a single command. ClusterControl will make SSH connections to each node, install and start the necessary software.

Step 1. EC2 Security Groups
---------------------------

Galera node needs to be accessible at several ports, so for simplicity it is better to allow all incoming ICMP and TCP packets from other nodes in the same security group. The default EC2 security group allows exactly this. You should also allow inbound http and ssh access, at least to the first node, which we will call *Control Instance* below.

Step 2. Start the Nodes
-----------------------

Start 4 or more of your favourite EC2 instances (this howto was prepared with RightImage_CentOS_5.6_x64_v5.7.14). You will need to choose one of the instances to run a control server and that may be anything, the rest of the instances will be Galera nodes and for best results they should be identical.

Step 3. Set Up the Keypair
--------------------------

Copy the keypair used to start the instances to the control instance, it will be used by deployment script to install and configure the rest of the nodes:

::

   scp -i keypair keypair root@<control_instance_public_dns_name>:

Step 4. Generate Deployment Package
-----------------------------------

Go to http://www.severalnines.com/galera-configurator/

Page 1 “General Settings”: specify cloud provider (Amazon EC2), number of instances, mysql root password, OS user (root)

Page 2 “Storage”: specify EC2 instance RAM size, planned db size, number of cores

Page 3 “Individual Nodes”:

* For ClusterControl server specify local IP address and the absolute path to keypair set up in step 3 (in this case /root/keypair).
* For Galera servers specify local IP addresses and the desirable location of data directory (RightScale images mounts local storage under /mnt)

After entering the required information you will be sent a tarball containing deployment scripts and there also will be a personal HTTP link at Severalnines site.

Step 5. Deploy from Control Instance
------------------------------------

This is how deployment goes. (Note that you'll get a different tarball link)

::

   [root@<control instance> ~]# wget http://www.severalnines.com/galera-configurator/tmp/mv00aikh65qmoju9p1qqlfaoo6/s9s-galera-1.0.0-rpm.tar.gz
   [root@<control instance> ~]# tar -xzf s9s-galera-1.0.0-rpm.tar.gz 
   [root@<control instance> ~]# cd ./s9s-galera-1.0.0-rpm/mysql/scripts/install
   [root@<control instance> install]# ./deploy.sh

Answer a couple of questions and sit back while ClusterControl connects to each EC2 instance, installs Galera and starts the cluster.

In a few minutes your cluster will be up and running. Success will be signified by an invitation to visit http://<control_instance_public_dns_name>/cmon/ to check how the cluster is doing. 

.. note:: You might want to set up HTTP password to protect the monitor from unauthorized access.

