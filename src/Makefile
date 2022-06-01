.PHONY=clean all
COMPILER=gcc
CFLAGS = -Wall -fsanitize=address -g
all: proxy
clean:	
	- rm -f *.o  proxy
rebuild: clean all

COMMON =  logger.c util.c
proxy:
	$(COMPILER) $(CFLAGS) -o tcpServerSelect proxy.c tcpServerUtil.c tcpClientUtil.c $(COMMON)