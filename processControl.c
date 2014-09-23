/*
 * processControl.c
 *
 *  Created on: 2014Äê9ÔÂ15ÈÕ
 *      Author: Dongyun
 */

#include "processControl.h"
#include "syscalls.h"
#include "protos.h"

#include "stdio.h"
#include "string.h"

INT32 numOfProcesses = 0;
INT32 pidToAssign = 0;
PCB *currentPCB = NULL;
TimerQueueNode *TimerQueue = NULL;
ReadyQueueNode *ReadyQueue = NULL;

void osCreateProcess(void *starting_address) {
	void *next_context;
	Z502MakeContext(&next_context, starting_address, USER_MODE);
	PCB *p = (PCB *) calloc(1, sizeof(PCB));
	p->pid = pidToAssign;
	strcpy(p->process_name, "osProcess");
	p->priority = 20;
	pidToAssign++;
	p->context = (Z502CONTEXT *) next_context;
	currentPCB = p;
	numOfProcesses++;
	Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE, &next_context);
}

void createProcess(char *process_name, void *entry, INT32 priority,
		INT32 *pidToReturn, INT32 *errCode) {
	INT32 checkResult = checkProcessParams(process_name, entry, priority);
	if (checkResult == ERR_SUCCESS) {
		PCB *p = (PCB *) calloc(1, sizeof(PCB));
		strcpy(p->process_name, process_name);
		void *next_context;
		Z502MakeContext(&next_context, entry, USER_MODE);
		p->context = (Z502CONTEXT *) next_context;
		p->pid = pidToAssign;
		p->priority = priority;
		ReadyQueueNode *node = (ReadyQueueNode *) calloc(1,
				sizeof(ReadyQueueNode));
		node->pcb = p;
		node->next = NULL;
		addToReadyQueue(node);
		numOfProcesses++;
		*pidToReturn = p->pid;
		pidToAssign++;
	}
	*errCode = checkResult;
}

void sleepProcess(INT32 timeToSleep) {
	INT32 startTime;
	CALL(MEM_READ(Z502ClockStatus, &startTime));
	TimerQueueNode *node = (TimerQueueNode *) calloc(1, sizeof(TimerQueueNode));
	node->pcb = currentPCB;
	node->time = startTime + timeToSleep - 6;
	node->next = NULL;
	INT32 result = addToTimerQueue(node);
	if (!TimerQueue->next || result == 1) {
		startTimer(timeToSleep);
	}
	if (ReadyQueue) {
		currentPCB = ReadyQueue->pcb;
		removeFromReadyQueue(currentPCB->pid);
		Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
				(void *) (&currentPCB->context));
	} else {
		Z502Idle();
	}
}

void terminateProcess(INT32 pid, INT32 *errCode) {
	if (pid == -2) {
		Z502Halt();
	}
	if (pid == -1) {
		if (!ReadyQueue && !TimerQueue) {
			Z502Halt();
		} else {
			free(currentPCB);
			currentPCB = ReadyQueue->pcb;
			*errCode = removeFromReadyQueue(currentPCB->pid);
			Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
					(void *) (&currentPCB->context));
		}
	} else {
		*errCode = removeFromReadyQueue(pid);
	}
	if (*errCode == ERR_SUCCESS) {
		numOfProcesses--;
	}
	if (numOfProcesses == 0) {
		Z502Halt();
	}
}

void getProcessID(char *process_name, INT32 *process_id, INT32 *errCode) {
	if (strcmp(process_name, "") == 0
			|| strcmp(process_name, currentPCB->process_name) == 0) {
		*process_id = currentPCB->pid;
		*errCode = ERR_SUCCESS;
		return;
	}

	ReadyQueueNode *p = ReadyQueue;
	while (p) {
		if (strcmp(p->pcb->process_name, process_name) == 0) {
			*process_id = p->pcb->pid;
			*errCode = ERR_SUCCESS;
			return;
		}
		p = (ReadyQueueNode *) p->next;
	}
	TimerQueueNode *q = TimerQueue;
	while (q) {
		if (strcmp(q->pcb->process_name, process_name) == 0) {
			*process_id = q->pcb->pid;
			*errCode = ERR_SUCCESS;
			return;
		}
		q = (TimerQueueNode *) q->next;
	}
	*errCode = ERR_BAD_PARAM;
	return;
}

void startTimer(INT32 timeToSet) {
	INT32 time = timeToSet;
	CALL(MEM_WRITE(Z502TimerStart, &time));
}

INT32 addToTimerQueue(TimerQueueNode *node) {
	if (!TimerQueue) {
		TimerQueue = node;
		return -1;
	}
	if (node->time < TimerQueue->time) {
		node->next = (INT32 *) TimerQueue;
		TimerQueue = node;
		return 1;
	}
	TimerQueueNode *p = TimerQueue;
	while ((TimerQueueNode *) p->next) {
		if (((TimerQueueNode *) p->next)->time > node->time) {
			break;
		}
		p = (TimerQueueNode *) p->next;
	}
	node->next = p->next;
	p->next = (INT32 *) node;
	return 0;
}

void removeFromTimerQueue() {
	if (!TimerQueue) {
		return;
	}
	if (TimerQueue->next) {
		startTimer(
				((TimerQueueNode *) TimerQueue->next)->time - TimerQueue->time);
	}
	PCB *pP = TimerQueue->pcb;
	ReadyQueueNode *node = (ReadyQueueNode *) calloc(1, sizeof(ReadyQueueNode));
	node->pcb = pP;
	node->next = NULL;
	addToReadyQueue(node);
	TimerQueueNode *pT = TimerQueue;
	TimerQueue = (TimerQueueNode *) TimerQueue->next;
	free(pT);
}

INT32 checkProcessParams(char *process_name, void *entry, INT32 priority) {
	if (numOfProcesses == MAX_NUMBER_OF_USER_THREADS - 1) {
		return ERR_BAD_PARAM;
	}
	if (!entry || (priority < 0)) {
		return ERR_BAD_PARAM;
	}
	if (!ReadyQueue && !TimerQueue) {
		return ERR_SUCCESS;
	}

	PCB *pP = currentPCB;
	if (strcmp(pP->process_name, process_name) == 0) {
		return ERR_BAD_PARAM;
	}

	ReadyQueueNode *pR = ReadyQueue;
	while (pR) {
		if (strcmp(pR->pcb->process_name, process_name) == 0) {
			return ERR_BAD_PARAM;
		}
		pR = (ReadyQueueNode *) pR->next;
	}

	TimerQueueNode *pT = TimerQueue;
	while (pT) {
		if (strcmp(pT->pcb->process_name, process_name) == 0) {
			return ERR_BAD_PARAM;
		}
		pT = (TimerQueueNode *) pT->next;
	}
	return ERR_SUCCESS;
}

void addToReadyQueue(ReadyQueueNode *node) {
	if (!ReadyQueue) {
		ReadyQueue = node;
		return;
	}
	if (ReadyQueue->pcb->priority < node->pcb->priority) {
		node->next = (INT32 *) ReadyQueue;
		ReadyQueue = node;
		return;
	}
	ReadyQueueNode *p = ReadyQueue;
	while ((ReadyQueueNode *) p->next) {
		if (((ReadyQueueNode *) p->next)->pcb->priority < node->pcb->priority) {
			break;
		}
		p = (ReadyQueueNode *) p->next;
	}
	node->next = p->next;
	p->next = (INT32 *) node;
}

INT32 removeFromReadyQueue(INT32 pid) {
	if (!ReadyQueue) {
		return ERR_BAD_PARAM;
	}
	ReadyQueueNode *p = ReadyQueue;
	if (ReadyQueue->pcb->pid == pid) {
		ReadyQueue = (ReadyQueueNode *) ReadyQueue->next;
		return ERR_SUCCESS;
	}
	while ((ReadyQueueNode *) p->next) {
		if (((ReadyQueueNode *) p->next)->pcb->pid == pid) {
			break;
		}
		p = (ReadyQueueNode *) p->next;
	}
	if (!p->next) {
		return ERR_BAD_PARAM;
	}
	ReadyQueueNode *q = (ReadyQueueNode *) p->next;
	p->next = q->next;
	free(q);
	return ERR_SUCCESS;
}
