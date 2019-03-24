#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <ws2tcpip.h>
#include <string.h>


#pragma comment(lib, "Ws2_32.lib")

#define _CRT_SECURE_NO_WARNINGS
void Init_Winsock();

int main(int argc, char** argv) {
	int listenfd = -1;
	int connfd = -1;
	int sockAddrInLength = sizeof(struct sockaddr_in);
	struct sockaddr_in receiver_addr, channel;
	if (argc != 3) {
		printf("Error: not enough arguments were provided!\n");
		exit(1);
	}
	Init_Winsock();

	if ((listenfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}

	unsigned int port = (unsigned int)strtoul(argv[1], NULL, 10); //get channel's port number
	memset(&receiver_addr, 0, sizeof(receiver_addr));
	receiver_addr.sin_family = AF_INET;
	receiver_addr.sin_port = htons(port);
	receiver_addr.sin_addr.s_addr = INADDR_ANY;

	if (0 != bind(listenfd, (struct sockaddr*) &receiver_addr, sizeof(receiver_addr))){
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	if (0 != listen(listenfd, 10)){
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	connfd = accept(listenfd, (struct sockaddr*) &channel, &sockAddrInLength); // implement that there is a thread handing stdin, and then opening a new socket and sending it to the channel
	if (connfd < 0)
	{
		fprintf(stderr, "%s\n", strerror(errno));
		closesocket(connfd);
	}



	WSACleanup();
	return 0;
}

void Init_Winsock() {
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR)
		printf("Error at WSAStartup()\n");
	exit(1);
}