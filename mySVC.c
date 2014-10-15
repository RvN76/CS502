/*
 * mySVC.c
 *
 *  Created on: 2014Äê10ÔÂ15ÈÕ
 *      Author: Dongyun
 */

#include "mySVC.h"
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

bool interruptFinished = true;

char operation[64];

void osCreateProcess(void *starting_address) {
	void *next_context;
	Z502MakeContext(&next_context, starting_address, USER_MODE);
	PCB *p = (PCB *) calloc(1, sizeof(PCB));
	p->pid = pidToAssign;
	strcpy(p->process_name, "osProcess");
	p->priority = 1;
	p->isSuspended = false;
	pidToAssign++;
	p->context = (Z502CONTEXT *) next_context;
	currentPCB = p;
	numOfProcesses++;
	Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE, &next_context);
}

void createProcess(char *process_name, void *entry, INT32 priority,
		INT32 *pidToReturn, INT32 *errCode) {
	sprintf(operation, "createProcess(%s, %p, %d)", process_name, entry,
			priority);
	getLock("INTERRUPT", USER);
	INT32 checkResult = checkProcessParams(process_name, entry, priority);
	if (checkResult == ERR_SUCCESS) {
		PCB *p = (PCB *) calloc(1, sizeof(PCB));
		strcpy(p->process_name, process_name);
		void *next_context;
		Z502MakeContext(&next_context, entry, USER_MODE);
		p->context = (Z502CONTEXT *) next_context;
		p->pid = pidToAssign;
		p->priority = priority;
		p->isSuspended = false;
		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = p;
		node->previous = NULL;
		node->next = NULL;
		addToReadyQueue(node);
		numOfProcesses++;
		*pidToReturn = p->pid;
		pidToAssign++;
	}
	releaseLock(USER);
	*errCode = checkResult;
}

void sleepProcess(INT32 timeToSleep) {
	sprintf(operation, "sleepProcess(%d)", timeToSleep);
	getLock("INTERRUPT", USER);
	if (!ReadyQueue && !TimerQueue) {
		releaseLock(USER);
		startTimer(timeToSleep);
		printf("reset by user: timer now added: %d\n", timeToSleep);
		Z502Idle();
	} else {
		if (timeToSleep <= 6) {
			timeToSleep = 0;
		} else {
			timeToSleep -= 6;
		}
		INT32 startTime, absoluteTime;
		CALL(MEM_READ(Z502ClockStatus, &startTime));
		absoluteTime = startTime + timeToSleep;
		TimerQueueNode *node = (TimerQueueNode *) calloc(1,
				sizeof(TimerQueueNode));
		node->pcb = currentPCB;
		node->time = absoluteTime;
		node->next = NULL;
		node->previous = NULL;
		addToTimerQueue(node);
		if (TimerQueue == node) {
			startTimer(timeToSleep);
			printf("reset by user (sleep): timer now to %d, pid = %d\n",
					node->time, node->pcb->pid);
		}
		if (ReadyQueue) {
			currentPCB = ReadyQueue->pcb;
			RSQueueNode *p = ReadyQueue;
			ReadyQueue = ReadyQueue->next;
			if (ReadyQueue) {
				ReadyQueue->previous = NULL;
			}
			free(p);
			printf("sr: Pid now to %d\n", currentPCB->pid);
			releaseLock(USER);
		} else {
			TimerQueueNode *p = TimerQueue;
			while (p != node) {
				if (p->pcb->isSuspended == false) {
					break;
				}
				p = p->next;
			}
			if (p != TimerQueue) {
				startTimer(p->time - startTime);
				printf("reset by user (sleep): timer now to %d, pid = %d\n",
						p->time, p->pcb->pid);
			}
			currentPCB = p->pcb;
			removeFromTimerQueue(p->pcb->pid, &p);
			startTime = p->time;
			free(p);
			printf("st: Pid now to %d\n", currentPCB->pid);
			interruptFinished = false;
			printf("sleep waiting for interrupt\n");
			releaseLock(USER);
			Z502Idle();
			while (!interruptFinished)
				;
			printf("waiting finished\n");
		}
		Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
				(void *) (&currentPCB->context));
	}
}

void wakeUpProcesses(bool currentTimeAcquired, INT32 *time) {
	if (!TimerQueue) {
		printf("interrupt finished\n");
		return;
	}
	INT32 currentTime;
	if (!currentTimeAcquired) {
		CALL(MEM_READ(Z502ClockStatus, &currentTime));
		if (time) {
			*time = currentTime;
		}
	} else {
		currentTime = *time;
	}
	printf("currentTime = %d\n", currentTime);
	TimerQueueNode *pT = TimerQueue;
	while (pT) {
		if (pT->time > currentTime) {
			startTimer(pT->time - currentTime);
			printf("reset by interrupt: timer now to %d, pid = %d\n", pT->time,
					pT->pcb->pid);
			break;
		}
		pT = pT->next;
	}

	TimerQueueNode *pT1 = TimerQueue, *pT2;
	RSQueueNode *node;
	while (pT1 != pT) {
		PCB *p = pT1->pcb;
		node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = p;
		node->next = NULL;
		node->previous = NULL;
		if (p->isSuspended == false) {
			addToReadyQueue(node);
		} else {
			addToSuspendQueue(node, &SuspendQueue);
		}
		pT2 = pT1;
		pT1 = pT1->next;
		free(pT2);
	}
	TimerQueue = pT;
	if (TimerQueue) {
		TimerQueue->previous = NULL;
	}
	printf("interrupt finished\n");
}

void suspendProcess(INT32 pid, INT32 *errCode) {
	sprintf(operation, "suspendProcess(%d)", pid);
	getLock("INTERRUPT", USER);
	INT32 result;
	if (pid == -1 || pid == currentPCB->pid) {

	} else {
		RSQueueNode *p = NULL;
		removeFromRSQueue(pid, &ReadyQueue, &p);
		if (!p) {
			TimerQueueNode *q = searchInTimerQueue(pid);
			if (q) {
				q->pcb->isSuspended = true;
			} else {
				result = ERR_BAD_PARAM;
			}
		} else {
			p->pcb->isSuspended = true;
			addToSuspendQueue(p, &SuspendQueue);
		}
		releaseLock(USER);
		*errCode = result;
		return;
	}
}

void resumeProcess(INT32 pid, INT32 *errCode) {
	sprintf(operation, "resumeProcess(%d)", pid);
	getLock("INTERRUPT", USER);
	RSQueueNode *p = NULL;
	INT32 result;
	removeFromRSQueue(pid, &SuspendQueue, &p);
	if (!p) {
		TimerQueueNode *q = searchInTimerQueue(pid);
		if (q && q->pcb->isSuspended == true) {
			result = ERR_SUCCESS;
			q->pcb->isSuspended = false;
		} else {
			result = ERR_BAD_PARAM;
		}
	} else {
		result = ERR_SUCCESS;
		p->pcb->isSuspended = false;
		addToReadyQueue(p);
	}
	releaseLock(USER);
	*errCode = result;
	return;
}

void terminateProcess(INT32 pid, INT32 *errCode) {
	INT32 pidToTerminate;
	if (pid == currentPCB->pid) {
		pidToTerminate = -1;
	} else {
		pidToTerminate = pid;
	}
	sprintf(operation, "terminateProcess(%d)", pidToTerminate);
	switch (pidToTerminate) {
	case -2:
		*errCode = ERR_SUCCESS;
		Z502Halt();
		break;
	case -1:
		*errCode = ERR_SUCCESS;
		numOfProcesses--;
		getLock("INTERRUPT", USER);
		if (ReadyQueue) {
			currentPCB = ReadyQueue->pcb;
			printf("tr: Pid now to %d\n", currentPCB->pid);
			RSQueueNode *p = ReadyQueue;
			ReadyQueue = ReadyQueue->next;
			if (ReadyQueue) {
				ReadyQueue->previous = NULL;
			}
			free(p);
			releaseLock(USER);
		} else {
			if (TimerQueue) {
				TimerQueueNode *p = TimerQueue;
				while (p) {
					if (p->pcb->isSuspended == false) {
						break;
					}
					p = p->next;
				}
				if (p) {
					currentPCB = p->pcb;
					printf("tt: Pid now to %d\n", currentPCB->pid);
					if (p != TimerQueue) {
						INT32 currentTime;
						CALL(MEM_READ(Z502ClockStatus, &currentTime));
						startTimer(p->time - currentTime);
						printf(
								"reset by user (terminate): timer now to %d, pid = %d\n",
								p->time, p->pcb->pid);
					}
					removeFromTimerQueue(p->pcb->pid, &p);
					free(p);
				} else {
					Z502Halt();
				}
				interruptFinished = false;
				releaseLock(USER);
				printf("terminate waiting for interrupt\n");
				Z502Idle();
				while (!interruptFinished)
					;
				printf("waiting finished\n");
			} else {
				Z502Halt();
			}
		}
		Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
				(void *) (&currentPCB->context));
		break;
	default: {
		getLock("INTERRUPT", USER);
		RSQueueNode *p = NULL;
		INT32 result;
		removeFromRSQueue(pidToTerminate, &ReadyQueue, &p);
		if (!p) {
			TimerQueueNode *q = NULL;
			removeFromTimerQueue(pidToTerminate, &q);
			if (!q) {
				result = ERR_BAD_PARAM;
			} else {
				result = ERR_SUCCESS;
				free(q);
			}
		} else {
			result = ERR_SUCCESS;
			free(p);
		}
		if (result == ERR_SUCCESS) {
			numOfProcesses--;
		}
		releaseLock(USER);
		*errCode = result;
		break;
	}
	}
}

void getProcessID(char *process_name, INT32 *process_id, INT32 *errCode) {
	sprintf(operation, "getProcessID(%s)", process_name);
	getLock("INTERRUPT", USER);
	if (strcmp(process_name, "") == 0
			|| strcmp(process_name, currentPCB->process_name) == 0) {
		*process_id = currentPCB->pid;
		*errCode = ERR_SUCCESS;
		releaseLock(USER);
//		tryingToHandle[USER] = false;
		return;
	}

	RSQueueNode *p = ReadyQueue;
	while (p) {
		if (strcmp(p->pcb->process_name, process_name) == 0) {
			*process_id = p->pcb->pid;
			*errCode = ERR_SUCCESS;
			releaseLock(USER);
			return;
		}
		p = p->next;
	}

	TimerQueueNode *q = TimerQueue;
	while (q) {
		if (strcmp(q->pcb->process_name, process_name) == 0) {
			*process_id = q->pcb->pid;
			*errCode = ERR_SUCCESS;
			releaseLock(USER);
			return;
		}
		q = q->next;
	}
	*errCode = ERR_BAD_PARAM;
	releaseLock(USER);
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
	p = TimerQueue;
//	printf("pid %d added to TimerQueue\n", node->pcb->pid);
//	while (p) {
//		printf("pid = %d, time = %d\n", p->pcb->pid, p->time);
//		p = p->next;
//	}
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

void removeFromTimerQueue(INT32 pid, TimerQueueNode **p) {
	*p = searchInTimerQueue(pid);
	if (!(*p)) {
		return;
	} else {
		if ((*p) == TimerQueue) {
			TimerQueue = (*p)->next;
			if (TimerQueue) {
				TimerQueue->previous = NULL;
			}
		} else {
			(*p)->previous->next = (*p)->next;
			if ((*p)->next) {
				(*p)->next->previous = (*p)->previous;
			}
		}
		(*p)->previous = NULL;
		(*p)->next = NULL;
		return;
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

void addToReadyQueue(RSQueueNode *node) {
	if (!ReadyQueue) {
		ReadyQueue = node;
		return;
	}
	if (ReadyQueue->pcb->priority > node->pcb->priority) {
		node->next = ReadyQueue;
		ReadyQueue->previous = node;
		ReadyQueue = node;
		return;
	}
	RSQueueNode *p = ReadyQueue;
	while (p->next) {
		if (p->next->pcb->priority > node->pcb->priority) {
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

void addToSuspendQueue(RSQueueNode *node, RSQueueNode **queueHead) {
	if (!(*queueHead)) {
		*queueHead = node;
		return;
	}
	node->next = *queueHead;
	(*queueHead)->previous = node;
	*queueHead = node;
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

void removeFromRSQueue(INT32 pid, RSQueueNode **queueHead, RSQueueNode **p) {
	*p = searchInRSQueue(pid, *queueHead);
	if (!(*p)) {
		return;
	} else {
		if (*p == *queueHead) {
			*queueHead = (*p)->next;
			if (*queueHead) {
				(*queueHead)->previous = NULL;
			}
		} else {
			(*p)->previous->next = (*p)->next;
			if ((*p)->next) {
				(*p)->next->previous = (*p)->previous;
			}
		}
		(*p)->previous = NULL;
		(*p)->next = NULL;
		return;
	}
}

void getLock(char *lockHolder, INT32 lockCandidate) {
	INT32 potentialLockHolder = 1 - lockCandidate;
	tryingToHandle[lockCandidate] = true;
	prioritiveProcess = potentialLockHolder;
	bool c = true;
	while (tryingToHandle[potentialLockHolder] == true
			&& prioritiveProcess == potentialLockHolder) {
		if (c) {
			printf("%s is blocked by %s, wait\n", operation, lockHolder);
			c = false;
		}
		continue;
	}
}

void releaseLock(INT32 lockHolder) {
	tryingToHandle[lockHolder] = false;
}
