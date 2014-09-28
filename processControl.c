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
	p->priority = 1000;
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

	if (!ReadyQueue && !TimerQueue) {
		startTimer(timeToSleep);
		Z502Idle();
	} else {
		if (timeToSleep <= 6) {
			timeToSleep = 0;
		}
		INT32 startTime, absoluteTime;
		CALL(MEM_READ(Z502ClockStatus, &startTime));
		absoluteTime = startTime + timeToSleep;
		if (!ReadyQueue && absoluteTime < TimerQueue->time) {
			startTimer(timeToSleep);
			Z502Idle();
		} else {
			TimerQueueNode *node = (TimerQueueNode *) calloc(1,
					sizeof(TimerQueueNode));
			node->pcb = currentPCB;
			node->time = absoluteTime;
			node->next = NULL;
			addToTimerQueue(node);
			if (ReadyQueue) {
				if (TimerQueue == node) {
					startTimer(timeToSleep);
				}
				currentPCB = ReadyQueue->pcb;
				ReadyQueueNode *p = ReadyQueue;
				ReadyQueue = ReadyQueue->next;
				free(p);
			} else {
				currentPCB = TimerQueue->pcb;
				TimerQueueNode *p = TimerQueue;
				TimerQueue = TimerQueue->next;
				free(p);
				Z502Idle();
			}
			Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
					(void *) (&currentPCB->context));
		}
//		if (ReadyQueue) {
//			TimerQueueNode *node = (TimerQueueNode *) calloc(1,
//					sizeof(TimerQueueNode));
//			node->pcb = currentPCB;
//			node->time = absoluteTime;
//			node->next = NULL;
//			addToTimerQueue(node);
//			if (TimerQueue == node) {
//				startTimer(timeToSleep);
//			}
//			currentPCB = ReadyQueue->pcb;
//			ReadyQueueNode *p = ReadyQueue;
//			ReadyQueue = ReadyQueue->next;
//			free(p);
//			Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
//					(void *) (&currentPCB->context));
//		} else if (absoluteTime < TimerQueue->time) {
//			startTimer(timeToSleep);
//			Z502Idle();
//		} else {
//			TimerQueueNode *node = (TimerQueueNode *) calloc(1,
//					sizeof(TimerQueueNode));
//			node->pcb = currentPCB;
//			node->time = absoluteTime;
//			node->next = NULL;
//			addToTimerQueue(node);
//			currentPCB = TimerQueue->pcb;
//			TimerQueueNode *p = TimerQueue;
//			TimerQueue = TimerQueue->next;
//			free(p);
//			Z502Idle();
//			Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
//					(void *) (&currentPCB->context));
//		}
	}
}

void wakeUpProcesses() {
	if (!TimerQueue) {
		return;
	}
	INT32 currentTime;
	CALL(MEM_READ(Z502ClockStatus, &currentTime));
	TimerQueueNode *pT = TimerQueue;
	while (pT) {
		if (pT->time > currentTime) {
			break;
		}
		pT = pT->next;
	}
	if (pT) {
		startTimer(pT->time - currentTime);
	}

	TimerQueueNode *pT1 = TimerQueue, *pT2;
	ReadyQueueNode *node;
	while (pT1 != pT) {
		if (!(!currentPCB && pT1 == TimerQueue)) {
			PCB *p = pT1->pcb;
			node = (ReadyQueueNode *) calloc(1, sizeof(ReadyQueueNode));
			node->pcb = p;
			node->next = NULL;
			addToReadyQueue(node);
		}
		pT2 = pT1;
		pT1 = pT1->next;
		free(pT2);
	}
	TimerQueue = pT1;
}

void terminateProcess(INT32 pid, INT32 *errCode) {
	INT32 pidToTerminate;
	if (pid == currentPCB->pid) {
		pidToTerminate = -1;
	} else {
		pidToTerminate = pid;
	}
	INT32 result;
	switch (pidToTerminate) {
	case -2:
		*errCode = ERR_SUCCESS;
		Z502Halt();
		break;
	case -1:
		*errCode = ERR_SUCCESS;
		numOfProcesses--;
		if (!ReadyQueue) {
			if (TimerQueue) {
				currentPCB = TimerQueue->pcb;
				TimerQueueNode *p = TimerQueue;
				TimerQueue = TimerQueue->next;
				free(p);
				Z502Idle();
				Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
						(void *) &currentPCB->context);
			} else {
				Z502Halt();
			}
		} else {
			currentPCB = ReadyQueue->pcb;
			ReadyQueueNode *p = ReadyQueue;
			ReadyQueue = ReadyQueue->next;
			free(p);
			Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
					(void *) (&currentPCB->context));
		}
		break;
	default:
		result = removeFromReadyQueue(pidToTerminate);
		if (result != ERR_SUCCESS) {
			result = removeFromTimerQueue(pidToTerminate);
		}
		*errCode = result;
		if (result == ERR_SUCCESS) {
			numOfProcesses--;
		}
		break;
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
		p = p->next;
	}

	TimerQueueNode *q = TimerQueue;
	while (q) {
		if (strcmp(q->pcb->process_name, process_name) == 0) {
			*process_id = q->pcb->pid;
			*errCode = ERR_SUCCESS;
			return;
		}
		q = q->next;
	}
	*errCode = ERR_BAD_PARAM;
	return;
}

void startTimer(INT32 timeToSet) {
	INT32 time = timeToSet;
	if (time <= 5) {
		time = 0;
	}
	CALL(MEM_WRITE(Z502TimerStart, &time));
}

void addToTimerQueue(TimerQueueNode *node) {
	if (!TimerQueue) {
		TimerQueue = node;
		return;
	}
	if (node->time < TimerQueue->time) {
		node->next = TimerQueue;
		TimerQueue = node;
		return;
	}
	TimerQueueNode *p = TimerQueue;
	while (p->next) {
		if (p->next->time > node->time) {
			break;
		}
		p = p->next;
	}
	node->next = p->next;
	p->next = node;
	return;
}

INT32 removeFromTimerQueue(INT32 pid) {
	if (!TimerQueue) {
		return ERR_BAD_PARAM;
	}
	TimerQueueNode *p = TimerQueue;
	if (p->pcb->pid == pid) {
		TimerQueue = p->next;
		free(p);
		return ERR_SUCCESS;
	}
	while (p->next) {
		if (p->next->pcb->pid == pid) {
			break;
		}
		p = p->next;
	}
	if (!p->next) {
		return ERR_BAD_PARAM;
	}
	TimerQueueNode *q = p->next;
	p->next = q->next;
	free(q);
	return ERR_SUCCESS;
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
		pR = pR->next;
	}

	TimerQueueNode *pT = TimerQueue;
	while (pT) {
		if (strcmp(pT->pcb->process_name, process_name) == 0) {
			return ERR_BAD_PARAM;
		}
		pT = pT->next;
	}
	return ERR_SUCCESS;
}

void addToReadyQueue(ReadyQueueNode *node) {
	if (!ReadyQueue) {
		ReadyQueue = node;
		return;
	}
	if (ReadyQueue->pcb->priority < node->pcb->priority) {
		node->next = ReadyQueue;
		ReadyQueue = node;
		return;
	}
	ReadyQueueNode *p = ReadyQueue;
	while (p->next) {
		if (p->next->pcb->priority < node->pcb->priority) {
			break;
		}
		p = p->next;
	}
	node->next = p->next;
	p->next = node;
}

INT32 removeFromReadyQueue(INT32 pid) {
	if (!ReadyQueue) {
		return ERR_BAD_PARAM;
	}
	ReadyQueueNode *p = ReadyQueue;
	if (p->pcb->pid == pid) {
		ReadyQueue = p->next;
		free(p);
		return ERR_SUCCESS;
	}
	while (p->next) {
		if (p->next->pcb->pid == pid) {
			break;
		}
		p = p->next;
	}
	if (!p->next) {
		return ERR_BAD_PARAM;
	}
	ReadyQueueNode *q = p->next;
	p->next = q->next;
	free(q);
	return ERR_SUCCESS;
}
