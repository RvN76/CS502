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

typedef struct {
	INT32 pid;
	char process_name[16];
	INT32 priority;
	Z502CONTEXT *context;
} PCB;

typedef struct tNode{
	PCB *pcb;
	INT32 time;
	struct tNode *next;
} TimerQueueNode;

typedef struct rNode{
	PCB *pcb;
	struct rNode *next;
} ReadyQueueNode;

extern INT32 numOfProcesses;
extern INT32 pidToAssign;
extern PCB *currentPCB;
extern TimerQueueNode *TimerQueue;
extern ReadyQueueNode *ReadyQueue;

extern void osCreateProcess();

extern void createProcess();

extern void terminateProcess();

extern void getProcessID();

extern void sleepProcess();

extern void wakeUpProcesses();

extern void startTimer();

extern void addToTimerQueue();

extern INT32 removeFromTimerQueue();

extern INT32 checkProcessParams();

extern void addToReadyQueue();

extern INT32 removeFromReadyQueue();

