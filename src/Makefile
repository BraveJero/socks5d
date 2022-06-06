.PHONY=clean all
COMPILER=gcc
CFLAGS = -Wall -fsanitize=address -g
INC= -I../include
all: proxy
clean:	
	- rm -f *.o  proxy
rebuild: clean all

COMMON =  logger.c util.c
proxy:
	$(COMPILER) $(CFLAGS) $(INC) -o proxy proxy.c tcpServerUtil.c tcpClientUtil.c buffer.c $(COMMON)