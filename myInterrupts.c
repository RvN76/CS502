/*
 * MyInterrupts.c
 *
 *  Created on: 2014��9��16��
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
			addToReadyQueue(node);
		} else {
			getMyLock(SUSPENDQUEUELOCK);
			addToSuspendQueue(node);
			releaseMyLock(SUSPENDQUEUELOCK);
		}
	}
	//	reset the timer
	if (TimerQueue) {
		startTimer(TimerQueue->time - currentTime);
	}
	releaseMyLock(TIMERQUEUELOCK);
	releaseMyLock(READYQUEUELOCK);
//	schedulerPrinter(currentPCB->pid, currentPCB->pid, "INT_T", currentTime);
	InterruptFinished = true;
	//	printf("interrupt finished\n");
}

void diskInterrupt(INT32 disk_id) {
	DiskQueueNode *p = NULL, *p1 = NULL;
	getMyLock(READYQUEUELOCK);
	getMyLock(DISKQUEUELOCK);
//	State that the disk is free now
	DiskOccupation[disk_id - 1] = -1;
//	Find the first node with disk_id and put the PCB into ReadyQueue
	p = DiskQueue;
	while (p) {
		if (p->disk_id == disk_id) {
			p1 = p->next;
			if (p == DiskQueue) {
				DiskQueue = p->next;
				if (DiskQueue) {
					DiskQueue->previous = NULL;
				}
			} else {
				p->previous->next = p->next;
				if (p->next) {
					p->next->previous = p->previous;
				}
			}
			break;
		}
		p = p->next;
	}

	p->previous = p->next = NULL;
//	releaseMyLock(DISKQUEUELOCK);
	PCB *pcb = p->pcb;
	free(p);
	RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
	node->previous = node->next = NULL;
	node->pcb = pcb;
	addToReadyQueue(node);
//	getMyLock(DISKQUEUELOCK);
	releaseMyLock(READYQUEUELOCK);
//	See if there is another task on this disk. If so, start it.
	while (p1) {
		if (p1->disk_id == disk_id) {
//			State that the disk is occupied by the process of p1->pcb
			DiskOccupation[disk_id - 1] = p1->pcb->pid;
			MEM_WRITE(Z502DiskSetID, &p1->disk_id);
			MEM_WRITE(Z502DiskSetSector, &p1->sector);
			MEM_WRITE(Z502DiskSetBuffer, p1->data);
			MEM_WRITE(Z502DiskSetAction, &p1->action);
			INT32 Start = 0;
			MEM_WRITE(Z502DiskStart, &Start);
			break;
		}
		p1 = p1->next;
	}
	releaseMyLock(DISKQUEUELOCK);
	InterruptFinished = true;
}
