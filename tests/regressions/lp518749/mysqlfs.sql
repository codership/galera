-- MySQL dump 10.11
--
-- Host: vs1    Database: 
-- ------------------------------------------------------
-- Server version       5.1.42-debug
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Current Database: `mysqlfs`
--

DROP DATABASE IF EXISTS mysqlfs;
CREATE DATABASE /*!32312 IF NOT EXISTS*/ `mysqlfs` /*!40100 DEFAULT CHARACTER SET latin1 */;

USE `mysqlfs`;

--
-- Table structure for table `data_blocks`
--

SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
CREATE TABLE `data_blocks` (
  `inode` bigint(20) NOT NULL,
  `seq` int(10) unsigned NOT NULL,
  `data` blob,
  PRIMARY KEY (`inode`,`seq`)
) ENGINE=InnoDB;
SET character_set_client = @saved_cs_client;

--
-- Dumping data for table `data_blocks`
--

INSERT INTO `data_blocks` VALUES (3,0,'1\n2\n3\n4\n');

--
-- Table structure for table `inodes`
--

SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
CREATE TABLE `inodes` (
  `inode` bigint(20) NOT NULL,
  `inuse` int(11) NOT NULL DEFAULT '0',
  `deleted` tinyint(4) NOT NULL DEFAULT '0',
  `mode` int(11) NOT NULL DEFAULT '0',
  `uid` int(10) unsigned NOT NULL DEFAULT '0',
  `gid` int(10) unsigned NOT NULL DEFAULT '0',
  `atime` int(10) unsigned NOT NULL DEFAULT '0',
  `mtime` int(10) unsigned NOT NULL DEFAULT '0',
  `ctime` int(10) unsigned NOT NULL DEFAULT '0',
  `size` bigint(20) NOT NULL DEFAULT '0',
  PRIMARY KEY (`inode`),
  KEY `inode` (`inode`,`inuse`,`deleted`)
) ENGINE=InnoDB;
SET character_set_client = @saved_cs_client;

--
-- Dumping data for table `inodes`
--

INSERT INTO `inodes` VALUES (1,0,0,16877,0,0,1265561793,1265561793,1265561793,0);
INSERT INTO `inodes` VALUES (3,0,0,33188,1000,1000,1265561852,1265561852,1265561852,8);

/*!50003 SET @SAVE_SQL_MODE=@@SQL_MODE*/;

DELIMITER ;;
/*!50003 SET SESSION SQL_MODE="" */;;
/*!50003 CREATE */ /*!50017 DEFINER=`root`@`localhost` */ /*!50003 TRIGGER `drop_data` AFTER DELETE ON `inodes` FOR EACH ROW BEGIN DELETE FROM data_blocks WHERE inode=OLD.inode; END */;;

DELIMITER ;
/*!50003 SET SESSION SQL_MODE=@SAVE_SQL_MODE*/;

--
-- Table structure for table `tree`
--

SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
CREATE TABLE `tree` (
  `inode` int(10) unsigned NOT NULL,
  `parent` int(10) unsigned DEFAULT NULL,
  `name` varchar(255) NOT NULL,
  UNIQUE KEY `name` (`name`,`parent`),
  KEY `inode` (`inode`),
  KEY `parent` (`parent`)
) ENGINE=InnoDB;
SET character_set_client = @saved_cs_client;

--
-- Dumping data for table `tree`
--

INSERT INTO `tree` VALUES (1,NULL,'/');
INSERT INTO `tree` VALUES (3,1,'foo');

UPDATE inodes SET inuse = inuse + 1 WHERE inode=3;

/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;
