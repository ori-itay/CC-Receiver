#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <winsock2.h>
#include <stdlib.h>
#include <ws2tcpip.h>
#include <string.h>
#include <windows.h>


#pragma comment(lib, "Ws2_32.lib")

#define STR_LEN 20
#define R_C_BUFF 64
#define SEND_BUFF 4


void Init_Winsock();

int END_FLAG = 0;

int main(int argc, char** argv) {

	char r_c_buff[R_C_BUFF], file_write_buff[R_C_BUFF];
	int send_buff[SEND_BUFF];
	int tot_err_cnt = 0, tot_received = 0, tot_written_to_file = 0, tot_err_fixed = 0;
	int notwritten, totalsent, num_sent, bytes_read, notread, totalread;
	int listenfd = -1;
	int connfd = -1;
	int sockAddrInLength = sizeof(struct sockaddr_in);
	struct sockaddr_in receiver_addr, channel;
	if (argc != 3) {
		printf("Error: not enough arguments were provided!\n");
		exit(1);
	}
	Init_Winsock();

	FILE *fp = fopen(argv[2], "w");
	if (fp == NULL) {
		printf("Error opening file. exiting... \n");
		exit(1);
	}

	printf("Type �End� when done");

	if ((listenfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}

	unsigned int port = (unsigned int)strtoul(argv[1], NULL, 10); //get channel's port number
	memset(&receiver_addr, 0, sizeof(receiver_addr));
	receiver_addr.sin_family = AF_INET;
	receiver_addr.sin_port = htons(port);
	receiver_addr.sin_addr.s_addr = INADDR_ANY;

	if (0 != bind(listenfd, (struct sockaddr*) &receiver_addr, sizeof(receiver_addr))) {
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	if (0 != listen(listenfd, 10)) {
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	connfd = accept(listenfd, (struct sockaddr*) &channel, &sockAddrInLength); // implement that there is a thread handing stdin, and then opening a new socket and sending it to the channel
	if (connfd < 0)
	{
		fprintf(stderr, "%s\n", strerror(errno));
		closesocket(connfd);
	}


	HANDLE thread = CreateThread(NULL, 0, thread_end_listen, &connfd, 0, NULL);

	while (END_FLAG == 0) {
		notread = R_C_BUFF;
		bytes_read = 0;
		while (notread > 0) {
			bytes_read = recvfrom(connfd, r_c_buff+ bytes_read, R_C_BUFF, 0, 0, 0);
			if (bytes_read == -1) {
				fprintf(stderr, "%s\n", strerror(errno));
				exit(1);
			}
			totalread += bytes_read;
			notread -= num_sent;
		}
		
		if (bytes_read <= 0) { break; }

		//manipulate flipping on received bits, change in place in chnl_buff_1
		detect_fix_err(r_c_buff, file_write_buff, &tot_err_cnt, &tot_err_fixed);

		if (fwrite(file_write_buff, sizeof(char), bytes_read, fp) != bytes_read) {
			printf("Error writing to file. exiting... \n");
			exit(1);
		}
		tot_written_to_file += bytes_read;
	}


	//send back stats
	memset(send_buff, '\0', SEND_BUFF);
	send_buff[0] = tot_received; send_buff[1] = tot_written_to_file; send_buff[2] = tot_err_cnt; send_buff[3] = tot_err_fixed;

	notwritten = SEND_BUFF;
	totalsent = 0;
	while (notwritten > 0) {
		num_sent = sendto(connfd, send_buff + totalsent, notwritten, 0, &channel, sizeof(channel));
		if (num_sent == -1) {
			fprintf(stderr, "%s\n", strerror(errno));
			exit(1);
		}
		totalsent += num_sent;
		notwritten -= num_sent;
	}



	if (closesocket(connfd) != 0) {
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
	if (iResult != NO_ERROR)
		printf("Error at WSAStartup()\n");
	exit(1);
}



DWORD WINAPI thread_end_listen(void *param) {
	char str[STR_LEN];
	int status;
	int connfd = *((int*)param);

	while (1) {
		memset(str, '\0', STR_LEN);
		if (scanf("%s", str) > 0 && strcmp(str, "END") == 0) {
			status = shutdown(connfd, SD_RECEIVE);
			if (status) {
				printf("Error while closing socket. \n");
				_endthread(1);
			}
			END_FLAG = 1;
			_endthread(0);
		}
	}
}




void detect_fix_err(char r_c_buff_1[R_C_BUFF], char file_write_buff[R_C_BUFF], int *tot_err_cnt, int *tot_err_fixed) {

	int bit_ind, char_ind, block_ind, xor = 0, bit_pos, i, j, block_err_cnt, row_err, col_err;
	char curr_bit, last_byte_in_block, diff, mask;

	memset(file_write_buff, 0, R_C_BUFF);

	for (block_ind = 0; block_ind < 8; block_ind++) {
		block_err_cnt = 0; row_err = -1; col_err = -1;
		for (bit_ind = 0; bit_ind < R_C_BUFF; bit_ind++) {

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