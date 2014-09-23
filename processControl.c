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
PCB *TimerQueue = NULL;
PCB *ReadyQueue = NULL;

void osCreateProcess(void *starting_address) {
	void *next_context;
	Z502MakeContext(&next_context, starting_address, USER_MODE);
	PCB *p = (PCB *) calloc(1, sizeof(PCB));
	p->pid = pidToAssign;
	strcpy(p->process_name, "osProcess");
	p->priority = 20;
	p->time = -1;
	p->next = NULL;
	pidToAssign++;
	p->context = (Z502CONTEXT *) next_context;
	currentPCB = p;
	//addToReadyQueue(p);
	numOfProcesses++;
	Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &next_context);
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
		p->time = -1;
		p->pid = pidToAssign;
		p->priority = priority;
		addToReadyQueue(p);
		numOfProcesses++;
		*pidToReturn = p->pid;
		pidToAssign++;
	}
	*errCode = checkResult;
}

void sleepProcess(INT32 timeToSleep) {
	INT32 startTime;
	CALL(MEM_READ(Z502ClockStatus, &startTime));
	currentPCB->time = startTime + timeToSleep - 6;
	INT32 result = addToTimerQueue();
	if (!TimerQueue->next || result == 1) {
		startTimer(timeToSleep);
	}
	if (ReadyQueue) {
		currentPCB = ReadyQueue;
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
			currentPCB = ReadyQueue;
			removeFromReadyQueue(currentPCB->pid);
			Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
					(void *) (&currentPCB->context));
		}
	}
	*errCode = removeFromReadyQueue(pid);
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

	PCB *p = ReadyQueue;
	while (p) {
		if (strcmp(p->process_name, process_name) == 0) {
			*process_id = currentPCB->pid;
			*errCode = ERR_SUCCESS;
			return;
		}
		p = (PCB *) p->next;
	}
	p = TimerQueue;
	while (p) {
		if (strcmp(p->process_name, process_name) == 0) {
			*process_id = currentPCB->pid;
			*errCode = ERR_SUCCESS;
			return;
		}
		p = (PCB *) p->next;
	}
	*errCode = ERR_BAD_PARAM;
	return;
}

void startTimer(INT32 timeToSet) {
	INT32 time = timeToSet;
	CALL(MEM_WRITE(Z502TimerStart, &time));
}

INT32 addToTimerQueue() {
	if (!TimerQueue) {
		TimerQueue = currentPCB;
		return -1;
	}
	if (currentPCB->time < TimerQueue->time) {
		currentPCB->next = (INT32 *) TimerQueue;
		TimerQueue = currentPCB;
		return 1;
	}
	PCB *p = TimerQueue;
	while ((PCB *) p->next) {
		if (((PCB *) p->next)->time > currentPCB->time) {
			break;
		}
		p = (PCB *) p->next;
	}
	currentPCB->next = p->next;
	p->next = (INT32 *) currentPCB;
	return 0;
}

void removeFromTimerQueue() {
	if (!TimerQueue) {
		return;
	}
	if (TimerQueue->next) {
		startTimer(((PCB *) TimerQueue->next)->time - TimerQueue->time);
	}
	addToReadyQueue(TimerQueue);
	TimerQueue = (PCB *) TimerQueue->next;
}

INT32 checkProcessParams(char *process_name, void *entry, INT32 priority) {
	if (numOfProcesses == MAX_NUMBER_OF_USER_THREADS - 1) {
		return ERR_BAD_PARAM;
	}
	if (!entry || (priority < 0)) {
		return ERR_BAD_PARAM;
	}
	if (!ReadyQueue) {
		return ERR_SUCCESS;
	}

	PCB *p = currentPCB;
	if (strcmp(p->process_name, process_name) == 0) {
		return ERR_BAD_PARAM;
	}

	p = ReadyQueue;
	while (p) {
		if (strcmp(p->process_name, process_name) == 0) {
			return ERR_BAD_PARAM;
		}
		p = (PCB *) p->next;
	}

	p = TimerQueue;
	while (p) {
		if (strcmp(p->process_name, process_name) == 0) {
			return ERR_BAD_PARAM;
		}
		p = (PCB *) p->next;
	}
	return ERR_SUCCESS;
}

void addToReadyQueue(PCB *PCBToAdd) {
	if (!ReadyQueue) {
		ReadyQueue = PCBToAdd;
		return;
	}
	if (ReadyQueue->priority < PCBToAdd->priority) {
		PCBToAdd->next = (INT32 *) ReadyQueue;
		ReadyQueue = PCBToAdd;
		return;
	}
	PCB *p = ReadyQueue;
	while ((PCB *) p->next) {
		if (((PCB *) p->next)->priority < PCBToAdd->priority) {
			break;
		}
		p = (PCB *) p->next;
	}
	PCBToAdd->next = p->next;
	p->next = (INT32 *) PCBToAdd;
}

INT32 removeFromReadyQueue(INT32 pid) {
	if (!ReadyQueue) {
		return ERR_BAD_PARAM;
	}
	PCB *p = ReadyQueue;
	if (ReadyQueue->pid == pid) {
		ReadyQueue = (PCB *) ReadyQueue->next;
		return ERR_SUCCESS;
	}
	while ((PCB *) p->next) {
		if (((PCB *) p->next)->pid == pid) {
			break;
		}
		p = (PCB *) p->next;
	}
	if (!p->next) {
		return ERR_BAD_PARAM;
	}

	PCB *q = (PCB *) p->next;
	p->next = q->next;
	return ERR_SUCCESS;
}
