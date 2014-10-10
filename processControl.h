/*
 * processControl.h
 *
 *  Created on: 2014Äê9ÔÂ15ÈÕ
 *      Author: Dongyun
 */

#ifndef PROCESSCONTROL_H_
#define PROCESSCONTROL_H_

#endif /* PROCESSCONTROL_H_ */

#include "global.h"
#include "Z502.h"

#include "stdlib.h"
#include "stdbool.h"

#define USER 		1
#define INTERRUPT	0

typedef struct {
	INT32 pid;
	char process_name[16];
	INT32 priority;
	bool isSuspended;
	Z502CONTEXT *context;
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

extern INT32 numOfProcesses;
extern INT32 pidToAssign;
extern PCB *currentPCB;
extern TimerQueueNode *TimerQueue;
extern RSQueueNode *ReadyQueue;
extern RSQueueNode *SuspendQueue;

extern INT32 prioritiveProcess;
extern bool tryingToHandle[2];

extern void osCreateProcess();

extern void createProcess();

extern void terminateProcess();

extern void getProcessID();

extern void sleepProcess();

extern void wakeUpProcesses();

extern void suspendProcess();

extern void resumeProcess();

extern void startTimer();

extern void addToTimerQueue();

extern TimerQueueNode *searchInTimerQueue();

extern void removeFromTimerQueue();

extern INT32 checkProcessParams();

extern void addToRSQueue();

extern RSQueueNode *searchInRSQueue();

extern void removeFromRSQueue();

