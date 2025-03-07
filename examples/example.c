// PERSIMQ demo example
#include <stdio.h>
#include <stdlib.h>
#include "persimq.h"

int main(void)
{
	PERSIMQ_set_debug_verbosity(PERSIMQ_VERBOSITY_DEBUG);
	//PERSIMQ_set_debug_verbosity(PERSIMQ_VERBOSITY_SILENT);
	
	T_PERSIMQ mq_add;
	printf("main(): Ready!\n"); fflush(stdout);
	if (!PERSIMQ_open(&mq_add, "test.dat", 64)) {
		perror("main(): PERSIMQ_open"); exit(1);
	}
	printf("main(): Open 1!\n"); fflush(stdout);
	char message[] = "Test message !!!";
	if (!PERSIMQ_push(&mq_add, message, sizeof(message))) {
		perror("main(): PERSIMQ_push return false"); exit(1);
	}
	printf("Added message: [%s]\n", message); fflush(stdout);
	
	if (!PERSIMQ_close(&mq_add) ) {
		perror("main(): PERSIMQ_close"); exit(1);
	}
	printf("Closed 1!\n"); fflush(stdout);

	T_PERSIMQ mq_extract;
	if (!PERSIMQ_open(&mq_extract, "test.dat", 64) ) {
		perror("main(): PERSIMQ_open"); exit(1);
	}
	
	if (PERSIMQ_is_empty(&mq_extract)) {
		printf("main(): The buffer is empty!\n"); fflush(stdout);
		if (!PERSIMQ_close(&mq_extract)) {
			perror("main(): PERSIMQ_close"); exit(1);
		}
		printf("main(): Closed 2!\n"); fflush(stdout);
		return 0;
	}
	
	printf("main(): Open 2!\n"); fflush(stdout);
	char buf[100];
	if (!PERSIMQ_get(&mq_extract, buf, sizeof(buf), NULL)) {
		perror("main(): PERSIMQ_get"); exit(1);
	}
	printf("main(): Get 2!\n"); fflush(stdout);
	printf("main(): Extracted message: [%s]\n", buf); fflush(stdout);
	if (!PERSIMQ_pop(&mq_extract)) {
		perror("main(): PERSIMQ_pop"); exit(1);
	}
	printf("main(): Pop 2!\n"); fflush(stdout);
	
	if (!PERSIMQ_close(&mq_extract)) {
		perror("main(): PERSIMQ_close"); exit(1);
	}
	printf("main(): Closed 2!\n"); fflush(stdout);	

	return 0;
}

