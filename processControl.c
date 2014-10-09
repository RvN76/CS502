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
RSQueueNode *ReadyQueue = NULL;
RSQueueNode *SuspendQueue = NULL;

INT32 prioritiveProcess = -1;
bool tryingToHandle[2] = { false, false };

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
	tryingToHandle[USER] = true;
	prioritiveProcess = INTERRUPT;
	int c = 0;
	while (tryingToHandle[INTERRUPT] == true && prioritiveProcess == INTERRUPT) {
		if (c == 0) {
			printf("blocked by INTERRUPT, wait\n");
			c = 1;
		}
		continue;
	}
	INT32 checkResult = checkProcessParams(process_name, entry, priority);
	if (checkResult == ERR_SUCCESS) {
		PCB *p = (PCB *) calloc(1, sizeof(PCB));
		strcpy(p->process_name, process_name);
		void *next_context;
		Z502MakeContext(&next_context, entry, USER_MODE);
		p->context = (Z502CONTEXT *) next_context;
		p->pid = pidToAssign;
		p->priority = priority;
		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = p;
		node->previous = NULL;
		node->next = NULL;
		addToRSQueue(node, &ReadyQueue);
		numOfProcesses++;
		*pidToReturn = p->pid;
		pidToAssign++;
	}
	tryingToHandle[USER] = false;
	*errCode = checkResult;
}

void sleepProcess(INT32 timeToSleep) {
	tryingToHandle[USER] = true;
	prioritiveProcess = INTERRUPT;
	int c = 0;
	while (tryingToHandle[INTERRUPT] == true && prioritiveProcess == INTERRUPT) {
		if (c == 0) {
			printf("blocked by INTERRUPT, wait\n");
			c = 1;
		}
		continue;
	}
	if (!ReadyQueue && !TimerQueue) {
		tryingToHandle[USER] = false;
		startTimer(timeToSleep);
		Z502Idle();
	} else {
		tryingToHandle[USER] = false;
		if (timeToSleep <= 6) {
			timeToSleep = 0;
		} else {
			timeToSleep -= 6;
		}
		INT32 startTime, absoluteTime;
		CALL(MEM_READ(Z502ClockStatus, &startTime));
		absoluteTime = startTime + timeToSleep;
		tryingToHandle[USER] = true;
		prioritiveProcess = INTERRUPT;
		int c1 = 0;
		while (tryingToHandle[INTERRUPT] == true && prioritiveProcess == INTERRUPT) {
			if (c1 == 0) {
				printf("blocked by INTERRUPT, wait\n");
				c1 = 1;
			}
			continue;
		}
		if (!ReadyQueue && absoluteTime < TimerQueue->time) {
			tryingToHandle[USER] = false;
			startTimer(timeToSleep);
			Z502Idle();
		} else {
			TimerQueueNode *node = (TimerQueueNode *) calloc(1,
					sizeof(TimerQueueNode));
			node->pcb = currentPCB;
			node->time = absoluteTime;
			node->next = NULL;
			node->previous = NULL;
			addToTimerQueue(node);
			if (ReadyQueue) {
				if (TimerQueue == node) {
					startTimer(timeToSleep);
				}
				currentPCB = ReadyQueue->pcb;
				RSQueueNode *p = ReadyQueue;
				ReadyQueue = ReadyQueue->next;
				if (ReadyQueue) {
					ReadyQueue->previous = NULL;
				}
				free(p);
				tryingToHandle[USER] = false;
			} else {
				currentPCB = TimerQueue->pcb;
				TimerQueueNode *p = TimerQueue;
				TimerQueue = TimerQueue->next;
				if (TimerQueue) {
					TimerQueue->previous = NULL;
				}
				free(p);
				tryingToHandle[USER] = false;
				Z502Idle();
			}
			Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
					(void *) (&currentPCB->context));
		}
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
	RSQueueNode *node;
	while (pT1 != pT) {
		PCB *p = pT1->pcb;
		node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = p;
		node->next = NULL;
		node->previous = NULL;
		addToRSQueue(node, &ReadyQueue);
		pT2 = pT1;
		pT1 = pT1->next;
		free(pT2);
	}
	TimerQueue = pT1;
	if (TimerQueue) {
		TimerQueue->previous = NULL;
	}
}

//void suspendProcess(INT32 pid, INT32 *errCode) {
//	if (pid == -1 || pid == currentPCB->pid) {
//		//pid = currentPCB->pid;
//		RSQueueNode *p = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
//		p->pcb = currentPCB;
//		p->next = NULL;
//		addToSuspendQueue(p);
//		if (ReadyQueue) {
//			currentPCB = ReadyQueue->pcb;
//			RSQueueNode *q = ReadyQueue;
//			ReadyQueue = ReadyQueue->next;
//			free(q);
//		} else {
//			currentPCB = TimerQueue->pcb;
//			TimerQueueNode *q = TimerQueue;
//			TimerQueue = TimerQueue->next;
//			free(q);
//			Z502Idle();
//		}
//		*errCode = ERR_SUCCESS;
//		Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
//				(void *) (&currentPCB->context));
//	} else {
//		RSQueueNode *p = searchInRSQueue(pid);
//		if (!p) {
//			*errCode = ERR_SUCCESS;
//		}
//	}
//}

void resumeProcess(INT32 pid, INT32 *errCode) {

}

void terminateProcess(INT32 pid, INT32 *errCode) {
	INT32 pidToTerminate;
	if (pid == currentPCB->pid) {
		pidToTerminate = -1;
	} else {
		pidToTerminate = pid;
	}
	switch (pidToTerminate) {
	case -2:
		*errCode = ERR_SUCCESS;
		Z502Halt();
		break;
	case -1:
		*errCode = ERR_SUCCESS;
		numOfProcesses--;
		tryingToHandle[USER] = true;
		prioritiveProcess = INTERRUPT;
		int c = 0;
		while (tryingToHandle[INTERRUPT] == true && prioritiveProcess == INTERRUPT) {
			if (c == 0) {
				printf("blocked by INTERRUPT, wait\n");
				c = 1;
			}
			continue;
		}
		if (!ReadyQueue) {
			if (TimerQueue) {
				currentPCB = TimerQueue->pcb;
				TimerQueueNode *p = TimerQueue;
				TimerQueue = TimerQueue->next;
				if (TimerQueue) {
					TimerQueue->previous = NULL;
				}
				free(p);
				tryingToHandle[USER] = false;
				Z502Idle();
				Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
						(void *) &currentPCB->context);
			} else {
				Z502Halt();
			}
		} else {
			currentPCB = ReadyQueue->pcb;
			RSQueueNode *p = ReadyQueue;
			ReadyQueue = ReadyQueue->next;
			if (ReadyQueue) {
				ReadyQueue->previous = NULL;
			}
			free(p);
			tryingToHandle[USER] = false;
			Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
					(void *) (&currentPCB->context));
		}
		break;
	default: {
		tryingToHandle[USER] = true;
		prioritiveProcess = INTERRUPT;
		int c = 0;
		while (tryingToHandle[INTERRUPT] == true && prioritiveProcess == INTERRUPT) {
			if (c == 0) {
				printf("blocked by INTERRUPT, wait\n");
				c = 1;
			}
			continue;
		}
		INT32 result = removeFromRSQueue(pidToTerminate, &ReadyQueue);
		if (result != ERR_SUCCESS) {
			result = removeFromTimerQueue(pidToTerminate);
		}
		if (result == ERR_SUCCESS) {
			numOfProcesses--;
		}
		*errCode = result;
		tryingToHandle[USER] = false;
		break;
	}
	}
}

void getProcessID(char *process_name, INT32 *process_id, INT32 *errCode) {
	tryingToHandle[USER] = true;
	prioritiveProcess = INTERRUPT;
	int c = 0;
	while (tryingToHandle[INTERRUPT] == true && prioritiveProcess == INTERRUPT) {
		if (c == 0) {
			printf("blocked by INTERRUPT, wait\n");
			c = 1;
		}
		continue;
	}
	if (strcmp(process_name, "") == 0
			|| strcmp(process_name, currentPCB->process_name) == 0) {
		*process_id = currentPCB->pid;
		*errCode = ERR_SUCCESS;
		tryingToHandle[USER] = false;
		return;
	}

	RSQueueNode *p = ReadyQueue;
	while (p) {
		if (strcmp(p->pcb->process_name, process_name) == 0) {
			*process_id = p->pcb->pid;
			*errCode = ERR_SUCCESS;
			tryingToHandle[USER] = false;
			return;
		}
		p = p->next;
	}

	TimerQueueNode *q = TimerQueue;
	while (q) {
		if (strcmp(q->pcb->process_name, process_name) == 0) {
			*process_id = q->pcb->pid;
			*errCode = ERR_SUCCESS;
			tryingToHandle[USER] = false;
			return;
		}
		q = q->next;
	}
	*errCode = ERR_BAD_PARAM;
	tryingToHandle[USER] = false;
	return;
}

void startTimer(INT32 timeToSet) {
	INT32 time = timeToSet;
	if (time <= 5) {
		time = 0;
	} else {
		time -= 5;
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
		TimerQueue->previous = node;
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

	node->previous = p;
	node->next = p->next;
	if (p->next) {
		p->next->previous = node;
	}
	p->next = node;
	return;
}

TimerQueueNode *searchInTimerQueue(INT32 pid) {
	//TimerQueueNode *node = NULL;
	if (!TimerQueue) {
		return NULL;
	}
	TimerQueueNode *p = TimerQueue;
	while (p) {
		if (p->pcb->pid == pid) {
			return p;
		}
		p = p->next;
	}
	return NULL;
}

INT32 removeFromTimerQueue(INT32 pid) {
	TimerQueueNode *p = searchInTimerQueue(pid);
	if (!p) {
		return ERR_BAD_PARAM;
	} else {
		if (p == TimerQueue) {
			TimerQueue = p->next;
			if (TimerQueue) {
				TimerQueue->previous = NULL;
			}
		} else {
			p->previous->next = p->next;
			if (p->next) {
				p->next->previous = p->previous;
			}
		}
		free(p);
		return ERR_SUCCESS;
	}
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

	RSQueueNode *pR = ReadyQueue;
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

void addToRSQueue(RSQueueNode *node, RSQueueNode **queueHead) {
	if (!*queueHead) {
		*queueHead = node;
		return;
	}
	if ((*queueHead)->pcb->priority < node->pcb->priority) {
		node->next = *queueHead;
		(*queueHead)->previous = node;
		*queueHead = node;
		return;
	}
	RSQueueNode *p = *queueHead;
	while (p->next) {
		if (p->next->pcb->priority < node->pcb->priority) {
			break;
		}
		p = p->next;
	}
	node->next = p->next;
	node->previous = p;

	if (p->next) {
		p->next->previous = node;
	}
	p->next = node;
}

RSQueueNode *searchInRSQueue(INT32 pid, RSQueueNode *queueHead) {
	if (!queueHead) {
		return NULL;
	}
	RSQueueNode *p = queueHead;
	while (p) {
		if (p->pcb->pid == pid) {
			return p;
		}
		p = p->next;
	}
	return NULL;
}

INT32 removeFromRSQueue(INT32 pid, RSQueueNode **queueHead) {
	RSQueueNode *p = searchInRSQueue(pid, *queueHead);
	if (!p) {
		return ERR_BAD_PARAM;
	} else {
		if (p == *queueHead) {
			*queueHead = p->next;
			if (*queueHead) {
				(*queueHead)->previous = NULL;
			}
		} else {
			p->previous->next = p->next;
			if (p->next) {
				p->next->previous = p->previous;
			}
		}
		free(p);
		return ERR_SUCCESS;
	}
}
