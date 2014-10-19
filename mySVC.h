/*
 * mySVC.h
 *
 *  Created on: 2014��10��15��
 *      Author: Dongyun
 */

#ifndef MYSVC_H_
#define MYSVC_H_

#include "global.h"
#include "Z502.h"

#include "stdlib.h"
#include "stdbool.h"

#define USER 		1
#define INTERRUPT	0

#define	NOT_SUSPENDED		0
#define WAITING_FOR_MESSAGE	1
#define	SUSPENDED			2

#define LEGAL_PRIORITY_UPPER_BOUND	100

#define MESSAGE_BOX_CAPACITY		8
#define MESSAGE_LENGTH_UPPERBOUND	64

#define READYQUEUELOCK		0
#define TIMERQUEUELOCK		1
#define SUSPENDQUEUELOCK	2
#define PRINTERLOCK			3

//INT32 LockingArea[4];

INT32 LockingResult[4];

typedef struct pNode {
	INT32 pid;
	bool isAlive;
	struct pNode *next;
} PidNode;

typedef struct ms {
	INT32 sender;
	char content[MESSAGE_LENGTH_UPPERBOUND];
	INT32 sendLength;
	struct ms *next;
} Message;

typedef struct msBox {
	INT32 recipient;
	Message *head;
	Message *tail;
	INT32 size;
//	struct msBox *previous;
	struct msBox *next;
} MessageBox;

typedef struct {
	INT32 pid;
	char process_name[16];
	INT32 priority;
	INT32 suspended;
	Z502CONTEXT *context;
	MessageBox *messageBox;
} PCB;

typedef struct tNode {
	PCB *pcb;
	INT32 time;
	struct tNode *previous;
	struct tNode *next;
} TimerQueueNode;

typedef struct rNode {
	PCB *pcb;
	struct rNode *previous;
	struct rNode *next;
} RSQueueNode;

INT32 numOfProcesses;
INT32 pidToAssign;
PCB *currentPCB;
TimerQueueNode *TimerQueue;
RSQueueNode *ReadyQueue;
RSQueueNode *SuspendQueue;
//RSQueueNode *MessageSuspendQueue;

PidNode *PidEverExisted;

MessageBox *MessageBoxQueue;
MessageBox *BroadcastMessageBox;

//INT32 prioritiveProcess;
//bool tryingToHandle[2];

bool InterruptFinished;

//char operation[64];

void osCreateProcess(void *);

void createProcess(char *, void *, INT32, INT32 *, INT32 *);

void terminateProcess(INT32, INT32 *);

void getProcessID(char *, INT32 *, INT32 *);

void sleepProcess(INT32);

void wakeUpProcesses();

void suspendProcess(INT32, INT32 *);

void resumeProcess(INT32, INT32 *);

void changePriority(INT32, INT32, INT32 *);

void sendMessage(INT32, char *, INT32, INT32 *);

void receiveMessage(INT32, char *, INT32, INT32 *, INT32 *, INT32 *);

void dispatch();

void startTimer(INT32);

void addToTimerQueue(TimerQueueNode *);

TimerQueueNode *searchInTimerQueue(INT32);

void removeFromTimerQueue(INT32, TimerQueueNode **);

INT32 checkProcessParams(char *, void *, INT32);

void addToReadyQueue(RSQueueNode *);

void addToSuspendQueue(RSQueueNode *);

RSQueueNode *searchInRSQueue(INT32, RSQueueNode *);

void removeFromRSQueue(INT32, RSQueueNode **, RSQueueNode **);

void addMessageBox(INT32);

void removeMessageBox(INT32);

void addNewPid(INT32);

void killPid(INT32);

void getMyLock(INT32);

void releaseMyLock(INT32);

void schedulerPrinter(INT32, INT32, char *, INT32);

#endif /* MYSVC_H_ */
