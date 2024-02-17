app=myfs

common=../common
obj=$(common)/config.o \
	$(common)/db.o \
	$(common)/log.o \
	$(common)/string.o \
	create.o \
	main.o \
	myfs.o \
	myfs_db.o \
	reclaimer.o \
	util.o

cc=gcc
cflags=`mariadb_config --cflags` `pkg-config fuse3 --cflags` -D_GNU_SOURCE -Wall -Wsign-compare -g2
libs=`mariadb_config --libs` `pkg-config fuse3 --libs`

all: $(app)

release: cflags:=$(filter-out -g2, $(cflags)) -O2
release: $(app)

$(app): $(obj)
	$(cc) -o $@ $^ $(libs)

%.o: %.c
	$(cc) -o $@ -c $< $(cflags)

clean:
	rm -f $(obj) $(app)

update: release
	mv $(app) /usr/local/bin

install: release
	mv $(app) /usr/local/bin
	mkdir -p /etc/myfs.d
	cp ../../etc/myfs.d/myfs.conf /etc/myfs.d
