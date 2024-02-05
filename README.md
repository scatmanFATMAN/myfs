# MyFS - A FUSE file system integrated with MariaDB.

## What is this?
MyFS is a user land file system written in C that implements the FUSE API to create a file system that manages its files in a MariaDB database. This is just a hobby project of mine because I was bored and wanted to learn something new, so I decided to learn FUSE. I didn't want to do something boring by simply creating files on the host file system again, so I thought creating them in a database might be kind of fun.


## How does it work?
+ It uses libfuse and implements the FUSE C API (https://github.com/libfuse/libfuse).\
+ It uses libmariadbclient and implements the MariaDB C API to manage the file system in the database (https://mariadb.com/docs/server/connect/programming-languages/c/).

MyFS gets mounted to a mount point like any other file system would. Once mounted, normal Linux commands are used to manage the file system in the MyFS mount point. FUSE passes those commands along to MyFS which then queries MariaDB to create, read, update, and delete data in the database that represent files on the file system. MyFS passes the results back to FUSE, which then presents the changes back to the user as if the changes were made on the actual file system.

This can almost be thought of as an NFS file system, where multiple clients can mount to a centralized location - except using a MariaDB database on the back end to present the file system.

MyFS currently only works on Linux and runs as whatever user/group started the file system. Files inherit MyFS's user/group with 0600 permissions while directories also inherit MyFS's user/group but with 0700 permissions.

## How to use it
There are two ways to setup MyFS.
1) Assisted Setup
  
    + You can use ```myfs --create true``` to have MyFS prompt you for setup configurations. It will install the configuration file and create the database and user automatically for you.
    + Run ```myfs --config-file <path-to-config-file>```
  
2) Manual Setup
   
  + The config file in ```etc/myfs.d/myfs.conf``` from this repository goes into ```/etc/myfs.d/myfs.conf``` on the file system. Update the configuration file with your configuration. You may rename it to whatever you want.
  + Create the MariaDB database and user.
    + Run ```myfs --print-create-sql``` to get the SQL statements needed to run.
  + Create the mount point.
  + Run ```myfs --config-file <path-to-config-file>```

## Features and Dreams (Maybe Some Hopes)
- [x] Core code, configuration, integrating FUSE and MariaDB.
- [ ] Implement all basic file system operations that FUSE supports.
    - [x] General
      - [x] Stat
      - [x] Rename
      - [ ] Change Ownership
      - [ ] Change Permissions
      - [ ] Locking
    - [x] Files
      - [x] Create
      - [x] Delete
      - [x] Open/Close
      - [x] Read
      - [x] Write
      - [x] Truncate
      - [ ] Seek
  - [x] Directories
      - [x] Create
      - [x] Delete
      - [x] Listing
  - [x] Symbolic Links
      - [x] Create
      - [x] Delete
      - [x] Read
- [ ]  Advanced and configurable logging to syslog or other interfaces.
- [ ]  Handle MariaDB query failures/retries. Block forever like NFS? This is important.
- [ ]  Command line switch or application to setup the initial database.
- [ ]  Finalize configuration file locations and how they're read.
- [ ]  Create RPM and Deb packages.
- [ ]  Support and test more configuration.

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
Not currently, but that'd be pretty cool down the line. If so, I suppose I'll need to think of a new project name (MyPostFS? MyFSPost? FSDB?). 

+ **Can I use Oracle?**\
No.

+ **Why would I use this?**\
You probably wouldn't.

+ **How do I create two or more consecutive blank lines in Markdown?**\
...\
...\
I hijacked my own FAQ but I must know!
