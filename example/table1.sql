CREATE TABLE `table1` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `hostname` varchar(50) NOT NULL,
  `cpu_idle` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `io` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `disk` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `net` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `load_status` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `memory` tinyint(4) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `hostname` (`hostname`)
) ENGINE=InnoDB AUTO_INCREMENT=4700 DEFAULT CHARSET=utf8;
