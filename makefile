LDLIBS=-ltar
CFLAGS=-ggdb
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
	./streamtar streamtar-test.tar streamtar-test/streamtar.c < streamtar.c
	./streamtar streamtar-test.tar streamtar-test/makefile < makefile
	tar tvf streamtar-test.tar
	[ "$$(tar -xOf streamtar-test.tar streamtar-test/makefile | sha256sum)" == "$$(cat makefile | sha256sum)" ]
	[ "$$(tar -xOf streamtar-test.tar streamtar-test/streamtar.c | sha256sum)" == "$$(cat streamtar.c | sha256sum)" ]
	rm streamtar-test.tar
