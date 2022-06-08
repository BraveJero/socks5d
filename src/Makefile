.PHONY = clean all
CFLAGS = -I../include -Wall -fsanitize=address -g -lpthread -std=c11 -pedantic -pedantic-errors -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200112L 
all: proxy
clean:	
	- rm -f *.o proxy

COMMON = logger.o util.o
SERVER_SOURCES = proxy.o clients.o tcpServerUtil.o selector.o tcpClientUtil.o buffer.o

proxy: $(COMMON) $(SERVER_SOURCES)
	$(CC) $(CFLAGS) -o proxy $(SERVER_SOURCES) $(COMMON)
	