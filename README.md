# MyFS - A FUSE file system integrated with MariaDB.
Version 0.1.0

## What is this?
MyFS is a user land file system written in C that implements the FUSE API to create a file system that manages its files in a MariaDB database. This is just a hobby project of mine because I was bored and wanted to learn something new, so I decided to learn FUSE. I didn't want to do something boring by simply creating files on the host file system again, so I thought creating them in a database might be kind of fun.


## How does it work?
+ It uses libfuse and implements the FUSE C API (https://github.com/libfuse/libfuse).
+ It uses libmariadbclient and implements the MariaDB C API to manage the file system in the database (https://mariadb.com/docs/server/connect/programming-languages/c/).

MyFS gets mounted to a mount point like any other file system would. Once mounted, normal Linux commands are used to manage the file system in the MyFS mount point. FUSE passes those commands along to MyFS which then queries MariaDB to create, read, update, and delete data in the database that represent files on the file system. MyFS passes the results back to FUSE, which then presents the changes back to the user as if the changes were made on the actual file system.

This can almost be thought of as an NFS file system, where multiple clients can mount to a centralized location - except using a MariaDB database on the back end to present the file system.

MyFS currently stores users and groups for files and directories but name, not UID or GID. This lets multiple MyFS clients on different hosts have differen UIDs and GIDs for the same named user or group. This also has a downside of that the user and group names must be sync'd and I'm not sure if this is the best way to do it yet.

## How to use it
There are two ways to setup MyFS.
1) Assisted Setup
  
    + You can use `myfs --create true` to have MyFS prompt you for setup configurations. It will create and install the configuration file, create the database and user, and create the mount point (if needed) for you.
    + Run `myfs --config-file <path-to-config-file>`
  
2) Manual Setup
   
    + The config file in `etc/myfs.d/myfs.conf` from this repository goes into `/etc/myfs.d/myfs.conf` on the file system, and may be renamed to whatever you'd like. Update that configuration file with your configuration.
    + Create the MariaDB database and user.
      + Run `myfs --print-create-sql` to get the SQL statements needed to run.
    + Create the mount point.
    + Run `myfs --config-file <path-to-config-file>`

## Supported Features
+ Create, delete, open, close, read, write, and truncate files.
+ Create, delete, and list directories.
+ Create and delete symbolic links.
+ Stat, rename (or move), and copy files and directories.
+ Change ownership of files and directories. Ownership is stored by user/group name instead of UID and GID. This means multiple MyFS clients do not need their UIDs and GIDs sync'd up, only their names. I'm not sure if this is better or worse honestly, but we'll see. I can always support both methods or go back to only storing UID and GID.
+ Change permissions on files and directories.
+ Uses block/page based data storage for files.
+ Atomic reads and writes using MariaDB transactions.
+ Configurable logging to syslog and/or stdout.
+ Configurable options for how to handle MariaDB query failures.
+ Configurable options for how to reclaim disk space when DELETEs occur.
+ Easy to use installation and setup using the `--create` command line switch and answering prompts.

## Not (Yet) Supported Features
+ File caching. Possibly put writes onto a background thread.
+ Hard links.
+ Auditing FUSE actions and putting them into the database.
+ Another binary or another config option to run OPTIMIZE TABLE and reclaim disk space. Maybe do it on a schedule.
+ Encryption? However this can be achieved through MariaDB native encryption very easily.

## Support
+ FUSE
  - [x] Version 3
+ Database
  - [x] MariaDB 10.3.x
+ Operating System
  - [x] Ubuntu 20.04.6

## FAQ
+ **What does MyFS stand for?**\
Since MyFS uses MariaDB as its database, it uses the same naming convention that Mari... Oh wait, that doesn't make sense. It uses the same naming convention that MySQL uses, which MariaDB is forked from.

+ **Can I use MySQL?**\
Most likely. I don't expect to use any MariaDB specific features so I'll be willing to bet that it works just fine.

+ **Can I use PostgreSQL?**\
Not currently, but that'd be pretty cool down the line.

+ **Can I use Oracle?**\
No.

+ **Why would I use this?**\
You probably wouldn't.
