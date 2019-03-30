#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <ws2tcpip.h>
#include <string.h>
#include <windows.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#pragma comment(lib, "Ws2_32.lib")

#define STR_LEN 20
#define UDP_BUFF 64
#define SEND_BUFF 4
#define WRITE_BUFF 49

int recvfromTimeOutUDP(SOCKET socket, long sec, long usec);
void Init_Winsock();
void send_frame(char buff[], int fd, struct sockaddr_in to_addr, int bytes_to_write);
int receive_frame(char buff[], int fd, int bytes_to_read, struct sockaddr_in *chnl_addr);
DWORD WINAPI thread_end_listen(void *param);
void detect_fix_err(char r_c_buff_1[UDP_BUFF], char file_write_buff[UDP_BUFF], int *tot_err_cnt, int *tot_err_fixed);
void extract_write_to_file(char file_write_buff[UDP_BUFF], FILE *fp);

volatile int END_FLAG = 0;
volatile int SelectTiming = 1;
volatile int s_fd = -1;
volatile unsigned short local_port;

int main(int argc, char** argv) {

	char r_c_buff[UDP_BUFF], file_write_buff[UDP_BUFF];
	int send_buff[SEND_BUFF] = { 0 };
	int tot_err_cnt = 0, tot_received = 0, tot_written_to_file = 0, tot_err_fixed = 0;
	int num_sent = 0, totalread = 0;
	int sockAddrInLength = sizeof(struct sockaddr_in);
	struct sockaddr_in recv_addr, chnl_addr;
	if (argc != 3) {
		fprintf(stderr, "Error: wrong number of arguments! Exiting...\n");
		exit(1);
	}

	Init_Winsock();
	
	FILE *fp = fopen(argv[2], "wb");
	if (fp == NULL) {
		fprintf(stderr,"Error opening file. Exiting...\n");
		exit(1);
	}

	printf("Type ''End'' when done.\n");

	if ((s_fd = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		fprintf(stderr, "Error: problem while opening socket. Exiting...\n");
		exit(1);
	}

	//channel address. other feilds determined in receive frame
	memset(&chnl_addr, 0, sizeof(chnl_addr));
	//receiver address
	local_port = (unsigned short)strtoul(argv[1], NULL, 10);
	memset(&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_port = htons(local_port);
	recv_addr.sin_addr.s_addr = INADDR_ANY;

	if (0 != bind(s_fd, (SOCKADDR *)&recv_addr, sizeof(recv_addr))) {
		fprintf(stderr, "Bind failed. Exiting...\n");
		return 1;
	}

	HANDLE thread = CreateThread(NULL, 0, thread_end_listen, NULL, 0, NULL);

	while (receive_frame(r_c_buff, s_fd, UDP_BUFF, &chnl_addr) == 0 && END_FLAG == 0) {
		tot_received += UDP_BUFF;
		detect_fix_err(r_c_buff, file_write_buff, &tot_err_cnt, &tot_err_fixed);
		extract_write_to_file(file_write_buff, fp);
		tot_written_to_file += WRITE_BUFF;
	}

	//send back stats
	send_buff[0] = tot_received; send_buff[1] = tot_written_to_file; send_buff[2] = tot_err_cnt; send_buff[3] = tot_err_fixed;
	send_frame((char*)send_buff, s_fd, chnl_addr, 16);

	if (closesocket(s_fd) != 0) {
		fprintf(stderr, "Error while closing socket. \n");
	}
	if (fclose(fp) != 0) {
		fprintf(stderr, "%s\n", strerror(errno));
	}

	fprintf(stderr, "received: %d bytes\nwrote: %d bytes\ndetected: %d errors, corrected: %d errors",
		tot_received, tot_written_to_file, tot_err_cnt, tot_err_fixed);

	WSACleanup();
	return 0;
}


void Init_Winsock() {
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR){
		fprintf(stderr,"Error at WSAStartup(). Exiting...\n");
		exit(1);
	}
}


DWORD WINAPI thread_end_listen(void *param) {
	char str[STR_LEN];
	int status;

	while (1) {
		memset(str, '\0', STR_LEN);
		if (scanf("%s", str) > 0 && strcmp(str, "End") == 0) {
			END_FLAG = 1;
			struct sockaddr_in send_itself;
			memset(&send_itself, 0, sizeof(send_itself));
			send_itself.sin_family = AF_INET;
			send_itself.sin_port = htons(local_port);
			send_itself.sin_addr.s_addr = inet_addr("127.0.0.1");
			status = sendto(s_fd, "0", 1, 0, (SOCKADDR*)&send_itself, sizeof(send_itself));
			if (status < 0) {
				fprintf(stderr, "Error while sending to socket. \n");
				exit(1);
			}
			return 0;
		}
	}
	return 0; // should not get here
}


void detect_fix_err(char r_c_buff[UDP_BUFF], char file_write_buff[UDP_BUFF], int *tot_err_cnt, int *tot_err_fixed) {

	int bit_ind, char_ind, block_ind, xor = 0, bit_pos, i, block_err_cnt, row_err, col_err;
	char curr_bit, last_byte_in_block, diff, mask;
	memset(file_write_buff, 0, UDP_BUFF);

	for (block_ind = 0; block_ind < 8; block_ind++) {
		block_err_cnt = 0; row_err = -1; col_err = -1; 
		for (bit_ind = 0; bit_ind < UDP_BUFF; bit_ind++) {
			
			if ( ( (bit_ind+1) % 8) == 0 && bit_ind != 0) {
				if (xor != (1 & r_c_buff[char_ind])) {
					block_err_cnt++;
					row_err = bit_ind / 8;
				}
				xor = 0;
				continue;
			}

			char_ind = (bit_ind / 8) + (block_ind * 8);
			bit_pos = 7 - (bit_ind % 7);

			curr_bit = (r_c_buff[char_ind] & ((int)pow(2, bit_pos))) != 0; // 1 if result after mask is different from 0. otherwise - 0.
			file_write_buff[char_ind] = (curr_bit << bit_pos) | r_c_buff[char_ind];
			xor ^= curr_bit;
		}

		last_byte_in_block = 0;
		for (i = 0; i < 7; i++) {
			last_byte_in_block ^= r_c_buff[i+block_ind*8];
		}
		diff = r_c_buff[7 + (block_ind * 8)] ^ last_byte_in_block;
		for (i = 0; i < 8; i++) {
			if (((int)pow(2, i) & diff) != 0) {
				block_err_cnt++;
				col_err = i;
			}
		}
		(*tot_err_cnt)+= (block_err_cnt != 0); // inc if any errors occured
		if (block_err_cnt == 2 && row_err!=-1 && col_err!=-1) { //one in row and one in column - fix bit
			mask = (char)pow(2, col_err);
			file_write_buff[row_err + (block_ind * 8)] ^= mask;
			(*tot_err_fixed)++;
		}
	}
	return;
}


void send_frame(char buff[], int fd, struct sockaddr_in to_addr, int bytes_to_write) {
	int totalsent = 0, num_sent = 0;

	while (bytes_to_write > 0) {
		num_sent = sendto(fd, buff + totalsent, bytes_to_write, 0, (SOCKADDR*)&to_addr, sizeof(to_addr));
		if (num_sent < 0) {
			fprintf(stderr, "Error while sending frame. \n");
			return;
		}
		totalsent += num_sent;
		bytes_to_write -= num_sent;
	}
}


int receive_frame(char buff[], int fd, int bytes_to_read, struct sockaddr_in *chnl_addr){
	int totalread = 0, bytes_been_read = 0, addrsize;
	struct sockaddr from_addr;
	memset(buff, '\0', UDP_BUFF);

	while (END_FLAG == 0 && totalread < bytes_to_read) {
		struct fd_set fds;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		int SelectTiming = select(fd + 1, &fds, NULL, NULL, NULL);
		if (END_FLAG == 1) {
			return 1;
		}
		if (FD_ISSET(fd, &fds)) {
			addrsize = sizeof(from_addr);
			bytes_been_read = recvfrom(fd, buff + totalread, bytes_to_read, 0, &from_addr, &addrsize);
			memcpy(chnl_addr, &from_addr, addrsize); // get channel address
			if (bytes_been_read < 0) {
				fprintf(stderr, "Error while receiving frame. \n");
				return 0;
			}
			totalread += bytes_been_read;
		}
	}
	return 0;
}


int recvfromTimeOutUDP(SOCKET socket, long sec, long usec)
{

	struct fd_set fds;
	int maxfd = (socket > 0) ? socket : 0;

	FD_ZERO(&fds);
	FD_SET(socket, &fds);
	FD_SET(0, &fds);

	return select(maxfd+1, &fds, NULL, NULL, NULL);
}


void extract_write_to_file(char file_write_buff[UDP_BUFF], FILE *fp) {
	int bit_ind, read_ind = 0, write_ind = 0, block_ind, r_bit_pos = 7, w_bit_pos = 7;
	char curr_bit;
	char extraction_buff[WRITE_BUFF] = { 0 };

	for (block_ind = 0; block_ind < 8; block_ind++) {
		for (bit_ind = 0; bit_ind < UDP_BUFF-8; bit_ind++) { // each row in block but the last one

			if ( (bit_ind+1) % 8 != 0) {
				curr_bit = (file_write_buff[read_ind] & (int)pow(2, r_bit_pos)) != 0; // 1 if result after mask is different from 0. otherwise - 0.
				extraction_buff[write_ind] = (curr_bit << w_bit_pos) | extraction_buff[write_ind];

				w_bit_pos--;
				if (w_bit_pos == -1) {
					w_bit_pos = 7;
					write_ind++;
				}
				r_bit_pos--;
				if (r_bit_pos == 0) {
					r_bit_pos = 7;
					read_ind++;
				}
			}
		}
		read_ind++;
	}




	if (fwrite(extraction_buff, sizeof(char), WRITE_BUFF, fp) != WRITE_BUFF) {
		fprintf(stderr, "Error writing to file.\n");
		return;
	}
}