/*
 * myTest.c
 *
 *  Created on: 2014Äê10ÔÂ19ÈÕ
 *      Author: Dongyun
 */

#include "myTest.h"
#include "syscalls.h"
#include "protos.h"

#include         "stdio.h"
#include         "string.h"
#include         "stdlib.h"

extern long Z502_REG1;
extern long Z502_REG2;
extern long Z502_REG3;
extern long Z502_REG4;
extern long Z502_REG5;
extern long Z502_REG6;
extern long Z502_REG7;
extern long Z502_REG8;
extern long Z502_REG9;
extern INT16 Z502_MODE;

extern void SuccessExpected(INT32, char[]);

extern void ErrorExpected(INT32, char[]);

void myTest1m() {
	char message[64];
	INT32 sender;
	INT32 sendLength;

	GET_PROCESS_ID("", &Z502_REG2, &Z502_REG9);
	SuccessExpected(Z502_REG9, "GET_PROCESS_ID");

	CHANGE_PRIORITY(-1, MOST_FAVORABLE_PRIORITY, &Z502_REG9);
	SuccessExpected(Z502_REG9, "CHANGE_PRIORITY");

//	create a myTest1m_1
	CREATE_PROCESS("myTest1m_1", myTest1m_1, NORMAL_PRIORITY, &Z502_REG3,
			&Z502_REG9);
	SuccessExpected(Z502_REG9, "CREATE_PROCESS");

//	Let the myTest1m_1 terminate itself
	SLEEP(200);

//  receive from myTest1m_1. It should fail because we can't find
//	message from it and it's already dead
	RECEIVE_MESSAGE(Z502_REG3, message, 64, &sendLength, &sender, &Z502_REG9);
	ErrorExpected(Z502_REG9, "RECEIVE MESSAGE");

//	create a myTest1m_2
	CREATE_PROCESS("myTest1m_2", myTest1m_2, NORMAL_PRIORITY, &Z502_REG4,
			&Z502_REG9);
	SuccessExpected(Z502_REG9, "CREATE_PROCESS");

//	receive from myTest1m_2. It should succeed because when test1m recovers from suspension
//  (woken by the send of myTest1m_2), it will find that though myTest1m_2 is dead already,
//  but it does have existed and we can find the message from it in the message box
	RECEIVE_MESSAGE(Z502_REG4, message, 64, &sendLength, &sender, &Z502_REG9);
	SuccessExpected(Z502_REG9, "RECEIVE MESSAGE");

	printf("Receive from PID %d, message is: %s\n", (INT32)Z502_REG4, message);

	TERMINATE_PROCESS(-2, &Z502_REG9);
}

void myTest1m_1() {
	TERMINATE_PROCESS(-1, &Z502_REG9);
}

void myTest1m_2() {
	SEND_MESSAGE(0, "This is myTest1m_2", 32, &Z502_REG9);
	SuccessExpected(Z502_REG9, "CREATE_PROCESS");

	TERMINATE_PROCESS(-1, &Z502_REG9);
}
