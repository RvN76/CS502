/*
 * MyInterrupts.c
 *
 *  Created on: 2014Äê9ÔÂ16ÈÕ
 *      Author: Dongyun
 */

#include "myInterrupts.h"
#include "mySVC.h"
#include "protos.h"
#include "syscalls.h"
#include "stdio.h"

void timerInterrupt() {
	INT32 currentTime;
	CALL(MEM_READ(Z502ClockStatus, &currentTime));

	TimerQueueNode *p = NULL;
	PCB *pcb = NULL;
	RSQueueNode *node = NULL;
	//	get the current time, put all nodes in TimerQueue whose time <= currentTime into
	//	ReadyQueue or SuspendQueue
	getMyLock(READYQUEUELOCK);
	getMyLock(TIMERQUEUELOCK);
	while (TimerQueue && TimerQueue->time <= currentTime) {
		p = TimerQueue;
		TimerQueue = p->next;
		if (TimerQueue) {
			TimerQueue->previous = NULL;
		}
		pcb = p->pcb;
		free(p);
		//		releaseMyLock(TIMERQUEUELOCK);
		node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = pcb;
		node->next = node->previous = NULL;
		if (pcb->suspended == NOT_SUSPENDED) {
			//			getMyLock(READYQUEUELOCK);
			addToReadyQueue(node);
			//			releaseMyLock(READYQUEUELOCK);
		} else {
			getMyLock(SUSPENDQUEUELOCK);
			addToSuspendQueue(node);
			releaseMyLock(SUSPENDQUEUELOCK);
		}
		//		getMyLock(TIMERQUEUELOCK);
	}
	//	reset the timer
	if (TimerQueue) {
		startTimer(TimerQueue->time - currentTime);
	}
	releaseMyLock(TIMERQUEUELOCK);
	releaseMyLock(READYQUEUELOCK);
	schedulerPrinter(currentPCB->pid, currentPCB->pid, "INTER", currentTime);
	InterruptFinished = true;
	return;
	//	printf("interrupt finished\n");
}

void diskInterrupt() {

}
