CREATE DATABASE `myfs`;

USE `myfs`;

CREATE TABLE `files` (
  `file_id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `parent_id` int(10) unsigned NOT NULL,
  `name` varchar(64) NOT NULL,
  `type` enum('File','Directory','Soft Link') NOT NULL,
  `content` longblob NOT NULL,
  `created_on` int(10) unsigned NOT NULL,
  `last_accessed_on` int(10) unsigned NOT NULL,
  `last_modified_on` int(10) unsigned NOT NULL,
  `last_status_changed_on` int(10) unsigned NOT NULL,
  PRIMARY KEY (`file_id`),
  KEY `fk_files_parentid` (`parent_id`),
  CONSTRAINT `fk_files_parentid` FOREIGN KEY (`parent_id`) REFERENCES `files` (`file_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE USER 'myfs'@'localhost' IDENTIFIED BY 'SomePassword123!';
GRANT USAGE ON *.* TO 'myfs'@'localhost';
GRANT EXECUTE, SELECT, SHOW VIEW, ALTER, ALTER ROUTINE, CREATE, CREATE ROUTINE, CREATE TEMPORARY TABLES, CREATE VIEW, DELETE, DROP, EVENT, INDEX, INSERT, REFERENCES, TRIGGER, UPDATE, LOCK TABLES  ON `myfs`.* TO 'myfs'@'localhost' WITH GRANT OPTION;
FLUSH PRIVILEGES;
