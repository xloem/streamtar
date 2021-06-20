LDLIBS=-ltar
CFLAGS=-ggdb -O0 -DDEBUG
PREFIX=/usr/local
all: streamtar
clean:
	-rm streamtar.tar
	-rm streamtar
install: streamtar
	cp "$<" "$(PREFIX)"/bin/
uninstall:
	rm "$(PREFIX)"/bin/streamtar
test: streamtar
	-rm streamtar-test.tar
	dd bs=$$((1024*1024)) count=$$((0177777777777/1024/1024)) if=/dev/zero | ./streamtar streamtar-test.tar streamtar-test/zeros
	./streamtar streamtar-test.tar streamtar-test/streamtar.c < streamtar.c
	./streamtar streamtar-test.tar streamtar-test/makefile < makefile
	tar tvf streamtar-test.tar
	test "$$(tar -xOf streamtar-test.tar streamtar-test/zeros | cksum)" = "$$(dd bs=$$((1024*1024)) iflag=count_bytes count=$$((077777777777)) if=/dev/zero | cksum)"
	test "$$(tar -xOf streamtar-test.tar streamtar-test/makefile | sha256sum)" = "$$(cat makefile | sha256sum)"
	test "$$(tar -xOf streamtar-test.tar streamtar-test/streamtar.c | sha256sum)" = "$$(cat streamtar.c | sha256sum)"
	rm streamtar-test.tar
