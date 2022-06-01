#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>   
#include <arpa/inet.h>    
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h> 
#include "logger.h"
#include "tcpServerUtil.h"
#include "tcpClientUtil.h"

#define max(n1,n2)     ((n1)>(n2) ? (n1) : (n2))

#define TRUE   1
#define FALSE  0
#define PORT_IPv4 8888
#define MAX_SOCKETS 30
#define BUFFSIZE 1024
#define MAX_PENDING_CONNECTIONS   3    // un valor bajo, para realizar pruebas
#define ORIGIN_PORT "9999"
#define ORIGIN "localhost"

struct buffer {
	char * buffer;
	size_t len;     // longitud del buffer
	size_t from;    // desde donde falta escribir
};

/**
  Se encarga de escribir la respuesta faltante en forma no bloqueante
  */
void handleWrite(int socket, struct buffer * buffer, fd_set * writefds);
/**
  Limpia el buffer de escritura asociado a un socket
  */
void clear( struct buffer * buffer);


int main(int argc , char *argv[])
{
	int opt = TRUE;
	int master_socket[2];  // IPv4 e IPv6 (si estan habilitados)
	int master_socket_size=0;
	int addrlen , new_socket , client_socket[MAX_SOCKETS][2] , max_clients = MAX_SOCKETS , activity, i , j, sd;
	long valread;
	int max_sd;
	struct sockaddr_in address;

	struct sockaddr_storage clntAddr; // Client address
	socklen_t clntAddrLen = sizeof(clntAddr);

	char buffer[BUFFSIZE + 1];  //data buffer of 1K

	//set of socket descriptors
	fd_set readfds;

	// Agregamos un buffer de escritura asociado a cada socket, para no bloquear por escritura
	struct buffer bufferWrite[MAX_SOCKETS][2];
	memset(bufferWrite, 0, sizeof bufferWrite);

	// y tambien los flags para writes
	fd_set writefds;

	//initialise all client_socket[] to 0 so not checked
	memset(client_socket, 0, sizeof(client_socket));

	// TODO adaptar setupTCPServerSocket para que cree socket para IPv4 e IPv6 y ademas soporte opciones (y asi no repetir codigo)
	
	// socket para IPv4 y para IPv6 (si estan disponibles)
	///////////////////////////////////////////////////////////// IPv4
	if( (master_socket[master_socket_size] = socket(AF_INET , SOCK_STREAM , 0)) == 0) 
	{
		log(ERROR, "socket IPv4 failed");
	} else {
		//set master socket to allow multiple connections , this is just a good habit, it will work without this
		if( setsockopt(master_socket[master_socket_size], SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 )
		{
			log(ERROR, "set IPv4 socket options failed");
		}

		//type of socket created
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons( PORT_IPv4 );

		// bind the socket to localhost port 8888
		if (bind(master_socket[master_socket_size], (struct sockaddr *)&address, sizeof(address))<0) 
		{
			log(ERROR, "bind for IPv4 failed");
			close(master_socket[master_socket_size]);
		}
		else {
			if (listen(master_socket[0], MAX_PENDING_CONNECTIONS) < 0)
			{
				log(ERROR, "listen on IPv4 socket failes");
				close(master_socket[master_socket_size]);
			} else {
				log(DEBUG, "Waiting for TCP IPv4 connections on socket %d\n", master_socket[master_socket_size]);
				master_socket_size++;
			}
		}
	}

	// Limpiamos el conjunto de escritura
	FD_ZERO(&writefds);
	while(TRUE) 
	{
		//clear the socket set
		FD_ZERO(&readfds);

		//add masters sockets to set
		for (int sdMaster=0; sdMaster < master_socket_size; sdMaster++)
			FD_SET(master_socket[sdMaster], &readfds);
			
		// add child sockets to set
		for(i =0; i < max_clients; i++) 
		{
			for(j = 0; j < 2; j++)
			{
			// socket descriptor
			sd = client_socket[i][j];

			// if valid socket descriptor then add to read list
			if(sd > 0)
				FD_SET( sd , &readfds);
			}
		}

		max_sd = master_socket[0];
		for(i =0; i < max_clients; i++) 
		{
			for(j = 0; j < 2; j++)
			{	
				sd = client_socket[i][j];
				if(sd > max_sd) 
					max_sd = sd;
			}
		}

		log(DEBUG, "Waiting for select...");

		//wait for an activity on one of the sockets , timeout is NULL , so wait indefinitely
		activity = select( max_sd + 1 , &readfds , &writefds , NULL , NULL);

		log(DEBUG, "select has something...");	

		if ((activity < 0) && (errno!=EINTR)) 
		{
			log(ERROR, "select error, errno=%d",errno);
			continue;
		}

		//If something happened on the TCP master socket , then its an incoming connection
		for (int sdMaster=0; sdMaster < master_socket_size; sdMaster++) {
			int mSock = master_socket[sdMaster];
			if (FD_ISSET(mSock, &readfds)) 
			{
				if ((new_socket = acceptTCPConnection(mSock)) < 0)
				{
					log(ERROR, "Accept error on master socket %d", mSock);
					continue;
				}

				// add new socket to array of sockets
				for (i = 0; i < max_clients; i++) 
				{
					// if position is empty
					if( client_socket[i][0] == 0 )
					{
						log(INFO, "Creando sock para comunicarme con origin");
						client_socket[i][0] = new_socket;
						// TODO: this is currently a blocking operation
						if((client_socket[i][1] = tcpClientSocket(ORIGIN, ORIGIN_PORT)) < 0) 
						{
							log(ERROR, "cannot open socket");
							close(new_socket);
							client_socket[i][0] = 0;
							client_socket[i][1] = 0;
						}
						log(DEBUG, "Adding to list of sockets as %d\n" , i);
						break;
					}
				}
			}
		}

		for(i =0; i < max_clients; i++) 
		{
			for(j = 0; j < 2; j++)
			{	
				sd = client_socket[i][j];

				if (FD_ISSET(sd, &writefds)) {
					handleWrite(sd, &bufferWrite[i][j], &writefds);
				}
			}
		}

		//else its some IO operation on some other socket :)
		for (i = 0; i < max_clients; i++) 
		{
			for(j = 0; j < 2; j++)
			{
				sd = client_socket[i][j];

				if (FD_ISSET( sd , &readfds)) 
				{
					//Check if it was for closing , and also read the incoming message
					if ((valread = read( sd , buffer, BUFFSIZE)) <= 0)
					{
						//Somebody disconnected , get his details and print
						getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
						log(INFO, "Host disconnected , ip %s , port %d \n" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));

						//Close the socket and mark as 0 in list for reuse
						close( sd );
						close( client_socket[i][1-j]);
						client_socket[i][j] = 0;
						client_socket[i][1-j] = 0;

						FD_CLR(sd, &writefds);
						FD_CLR(client_socket[i][1-j], &writefds);
						// Limpiamos el buffer asociado, para que no lo "herede" otra sesión
						clear(&bufferWrite[i][j]);
						clear(&bufferWrite[i][1-j]);
					}
					else {
						log(DEBUG, "Received %zu bytes from socket %d\n", valread, sd);
						// activamos el socket para escritura y almacenamos en el buffer de salida
						FD_SET(client_socket[i][1-j], &writefds);

						// Tal vez ya habia datos en el buffer
						// TODO: validar realloc != NULL
						bufferWrite[i][1-j].buffer = realloc(bufferWrite[i][1-j].buffer, bufferWrite[i][1-j].len + valread);
						memcpy(bufferWrite[i][1-j].buffer + bufferWrite[i][1-j].len, buffer, valread);
						bufferWrite[i][1-j].len += valread;
					}
				}
			}
		}
	}

	return 0;
}

void clear( struct buffer * buffer) {
	free(buffer->buffer);
	buffer->buffer = NULL;
	buffer->from = buffer->len = 0;
}

// Hay algo para escribir?
// Si está listo para escribir, escribimos. El problema es que a pesar de tener buffer para poder
// escribir, tal vez no sea suficiente. Por ejemplo podría tener 100 bytes libres en el buffer de
// salida, pero le pido que mande 1000 bytes.Por lo que tenemos que hacer un send no bloqueante,
// verificando la cantidad de bytes que pudo consumir TCP.
void handleWrite(int socket, struct buffer * buffer, fd_set * writefds) {
	size_t bytesToSend = buffer->len - buffer->from;
	if (bytesToSend > 0) {  // Puede estar listo para enviar, pero no tenemos nada para enviar
		log(INFO, "Trying to send %zu bytes to socket %d\n", bytesToSend, socket);
		size_t bytesSent = send(socket, buffer->buffer + buffer->from,bytesToSend,  MSG_DONTWAIT); 
		log(INFO, "Sent %zu bytes\n", bytesSent);

		if ( bytesSent < 0) {
			// Esto no deberia pasar ya que el socket estaba listo para escritura
			// TODO: manejar el error
			log(FATAL, "Error sending to socket %d", socket);
		} else {
			size_t bytesLeft = bytesSent - bytesToSend;

			// Si se pudieron mandar todos los bytes limpiamos el buffer y sacamos el fd para el select
			if ( bytesLeft == 0) {
				clear(buffer);
				FD_CLR(socket, writefds);
			} else {
				buffer->from += bytesSent;
			}
		}
	}
}


// client[0][0] tiene para mandarme ---> tengo que leer y mandarle a client[0][1]
