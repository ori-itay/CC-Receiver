#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <ws2tcpip.h>
#include <string.h>
#include <windows.h>
#include <math.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define STR_LEN 20
#define UDP_BUFF 64
#define SEND_BUFF 4

int recvfromTimeOutUDP(SOCKET socket, long sec, long usec);
void Init_Winsock();
void send_frame(char buff[], int fd, struct sockaddr_in to_addr, int bytes_to_write);
int receive_frame(char buff[], int fd, int bytes_to_read, struct sockaddr_in *chnl_addr);
DWORD WINAPI thread_end_listen(void *param);
void detect_fix_err(char r_c_buff_1[UDP_BUFF], char file_write_buff[UDP_BUFF], int *tot_err_cnt, int *tot_err_fixed);

volatile int END_FLAG = 0;
volatile int SelectTiming = 1;

int main(int argc, char** argv) {

	char r_c_buff[UDP_BUFF+1], file_write_buff[UDP_BUFF];
	int send_buff[SEND_BUFF];
	int tot_err_cnt = 0, tot_received = 0, tot_written_to_file = 0, tot_err_fixed = 0;
	int num_sent = 0, totalread = 0;
	int s_fd = -1;
	int sockAddrInLength = sizeof(struct sockaddr_in);
	struct sockaddr_in recv_addr, chnl_addr;
	if (argc != 3) {
		printf("Error: wrong number of arguments!\n");
		exit(1);
	}

	Init_Winsock();
	
	FILE *fp = fopen(argv[2], "w");
	if (fp == NULL) {
		printf("Error opening file. exiting... \n");
		exit(1);
	}

	printf("Type ''END'' when done. \n");

	if ((s_fd = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}

	//channel address. other feilds determined in receive frame
	memset(&chnl_addr, 0, sizeof(chnl_addr));
	//receiver address
	unsigned short local_port = (unsigned short)strtoul(argv[1], NULL, 10);
	memset(&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_port = htons(local_port);
	recv_addr.sin_addr.s_addr = INADDR_ANY;

	if (0 != bind(s_fd, (SOCKADDR *)&recv_addr, sizeof(recv_addr))) {
		fprintf(stderr, "Bind failed. exiting...\n");
		return 1;
	}

	//HANDLE thread = CreateThread(NULL, 0, thread_end_listen, &s_fd, 0, NULL);

	while (END_FLAG == 0 && receive_frame(r_c_buff, s_fd, UDP_BUFF, &chnl_addr) == 0) {

		detect_fix_err(r_c_buff, file_write_buff, &tot_err_cnt, &tot_err_fixed);

		if (fwrite(file_write_buff, sizeof(char), UDP_BUFF, fp) != UDP_BUFF) {
			printf("Error writing to file. exiting... \n");
			exit(1);
		}
		tot_written_to_file += UDP_BUFF;
	}

	//send back stats
	send_buff[0] = tot_received; send_buff[1] = tot_written_to_file; send_buff[2] = tot_err_cnt; send_buff[3] = tot_err_fixed;
	while (END_FLAG == 0) {}
	printf("before sending back to channel \n");
	send_frame((char*)send_buff, s_fd, chnl_addr, SEND_BUFF * sizeof(int));
	printf("after sending back to channel \n");
	if (closesocket(s_fd) != 0) {
		fprintf(stderr, "%s\n", strerror(errno));
	}
	if (fclose(fp) != 0) {
		fprintf(stderr, "%s\n", strerror(errno));
	}

	printf("received: %d bytes\nwrote: %d bytes\ndetected: %d errors, corrected: %d errors\n",
		tot_received, tot_written_to_file, tot_err_cnt, tot_err_fixed);

	WSACleanup();
	return 0;
}

void Init_Winsock() {
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR){
		printf("Error at WSAStartup()\n");
		exit(1);
	}
}



DWORD WINAPI thread_end_listen(void *param) {
	char str[STR_LEN];
	//int status;
	int fd = *((int*)param);

	while (1) {
		memset(str, '\0', STR_LEN);
		if (scanf("%s", str) > 0 && strcmp(str, "END") == 0) {
			END_FLAG = 1;
			printf("END FLAG IS UP\n");
			/*status = shutdown(fd, SD_RECEIVE);
			printf("SHUTDOWN SENT\n");
			if (status) {
				printf("Error while closing socket. \n");
				return -1;
			}*/
			return 0;
		}
	}
	return 0;
}




void detect_fix_err(char r_c_buff_1[UDP_BUFF], char file_write_buff[UDP_BUFF], int *tot_err_cnt, int *tot_err_fixed) {

	int bit_ind, char_ind, block_ind, xor = 0, bit_pos, i, block_err_cnt, row_err, col_err;
	char curr_bit, last_byte_in_block, diff, mask;

	memset(file_write_buff, 0, UDP_BUFF);

	for (block_ind = 0; block_ind < 8; block_ind++) {
		block_err_cnt = 0; row_err = -1; col_err = -1;
		for (bit_ind = 0; bit_ind < UDP_BUFF; bit_ind++) {

			if ((bit_ind % 7) == 0 && bit_ind != 0) {
				if (xor != (1 & r_c_buff_1[char_ind])) {
					block_err_cnt++;
					row_err = bit_ind / 8;
				}
				continue;
			}

			char_ind = (bit_ind / 8) + (block_ind * 8);
			bit_pos = 7 - bit_ind % 7;

			curr_bit = (r_c_buff_1[char_ind] & ((int)pow(2, bit_pos))) != 0; // 1 if result after mask is different from 0. otherwise - 0.
			file_write_buff[char_ind] = (curr_bit << bit_pos) | r_c_buff_1[char_ind];
			xor ^= curr_bit;
		}

		last_byte_in_block = 0;
		for (i = 0; i < 8; i++) {
			last_byte_in_block ^= r_c_buff_1[i];
		}
		diff = r_c_buff_1[7 + (block_ind * 8)] ^ last_byte_in_block;
		for (i = 0; i < 8; i++) {
			if (((int)pow(2, i) & diff) != 0) {
				block_err_cnt++;
				col_err = i;
			}
		}
		*tot_err_cnt += (block_err_cnt != 0); // inc if any errors occured
		if (block_err_cnt == 2) { //one in row and one in column - fix bit
			mask = (int)pow(2, col_err);
			r_c_buff_1[row_err + (block_ind * 8)] ^= mask;
			*tot_err_fixed++;
		}
	}

	return;
}


void send_frame(char buff[], int fd, struct sockaddr_in to_addr, int bytes_to_write) {
	int totalsent = 0, num_sent = 0;

	while (bytes_to_write > 0) {
		num_sent = sendto(fd, buff + totalsent, bytes_to_write, 0, (SOCKADDR*)&to_addr, sizeof(to_addr));
		if (num_sent < 0) {
			fprintf(stderr, "%s\n", strerror(errno));
			exit(1);
		}
		totalsent += num_sent;
		bytes_to_write -= num_sent;
	}
}


int receive_frame(char buff[], int fd, int bytes_to_read, struct sockaddr_in *chnl_addr){
	int totalread = 0, bytes_been_read = 0, addrsize;
	struct sockaddr from_addr;

	while (END_FLAG == 0 && totalread < bytes_to_read && SelectTiming > 0) {
		printf("L209  gonna block now\n");
		struct fd_set fds;
		int maxfd = (fd > 0) ? fd : 0;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		FD_SET(0, &fds); // 0 for STDIN
		fflush(0);
		int SelectTiming = select(maxfd + 1, &fds, NULL, NULL, NULL);
		if (FD_ISSET(0, &fd)) {
			char str[1024] = { 0 };
			if (scanf("%s", str) > 0 && strcmp(str, "END") == 0) {
				END_FLAG = 1;
				return 1;
			}
		}
		if (FD_ISSET(fd, &fds)) {
			bytes_been_read = recvfrom(fd, buff + totalread, bytes_to_read, 0, &from_addr, &addrsize);
			memcpy(chnl_addr, &from_addr, addrsize); // get channel address
			if (bytes_been_read < 0) {
				fprintf(stderr, "%s\n", strerror(errno));
				exit(1);
			}
			totalread += bytes_been_read;
		}
	}/*
	buff[UDP_BUFF] = 0;
	for (int i = 0; i < UDP_BUFF; i++) {
		printf(buff[i]);
	}
	printf("\n");*/
	return 0;
}


int recvfromTimeOutUDP(SOCKET socket, long sec, long usec)
{

	// Setup timeval variable




	//struct timeval timeout;

	struct fd_set fds;
	int maxfd = (socket > 0) ? socket : 0;

	FD_ZERO(&fds);
	FD_SET(socket, &fds);
	FD_SET(0, &fds);

	//timeout.tv_sec = sec;

	//timeout.tv_usec = usec;

	// Setup fd_set structure
	/*
	FD_ZERO(&fds);

	FD_SET(socket, &fds); */

	// Return value:

	// -1: error occurred

	// 0: timed out

	// > 0: data ready to be read

	return select(maxfd+1, &fds, NULL, NULL, NULL);

}