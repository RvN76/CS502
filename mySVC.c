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
MessageBox *MessageBoxQueue = NULL;
MessageBox *BroadcastMessageBox = NULL;

PidNode *PidEverExisted = NULL;

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
	p->messageBox = NULL;
	addNewPid(p->pid);
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
		p->messageBox = NULL;
		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = p;
		node->previous = NULL;
		node->next = NULL;
		addToReadyQueue(node);
		addNewPid(p->pid);
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
		dispatch("Sleep", startTime);
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
	INT32 result;
	if (pid == -1 || pid == currentPCB->pid) {
		*errCode = ERR_SUCCESS;
		getLock("INTERRUPT", USER);
		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode *));
		node->pcb = currentPCB;
		node->pcb->isSuspended = true;
		node->next = NULL;
		node->previous = NULL;
		addToSuspendQueue(node, &SuspendQueue);
		dispatch("Suspend", -1);
		Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
				(void *) (&currentPCB->context));
	} else {
		getLock("INTERRUPT", USER);
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
		killPid(currentPCB->pid);
		*errCode = ERR_SUCCESS;
		numOfProcesses--;
		getLock("INTERRUPT", USER);
		dispatch("Terminate", -1);
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
			killPid(pidToTerminate);
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
		releaseLock(USER);
		*errCode = ERR_SUCCESS;
		return;
	}

	RSQueueNode *p = NULL;
	int i;
	for (i = 0; i < 3; i++) {
		if (i == 0) {
			p = ReadyQueue;
		}
		if (i == 1) {
			p = SuspendQueue;
		}
		if (i == 2) {
			p = MessageSuspendQueue;
		}
		while (p) {
			if (strcmp(p->pcb->process_name, process_name) == 0) {
				*process_id = p->pcb->pid;
				releaseLock(USER);
				*errCode = ERR_SUCCESS;
				return;
			}
			p = p->next;
		}
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

void changePriority(INT32 pid, INT32 priority, INT32 *errCode) {
	if (priority <= 0 || priority > LEGAL_PRIORITY_UPPER_BOUND) {
		*errCode = ERR_BAD_PARAM;
		return;
	}
	getLock("INTEERUPT", USER);
	PCB *pcb = NULL;
	if (pid == currentPCB->pid || pid == -1) {
		pcb = currentPCB;
	} else {
		TimerQueueNode *pT = searchInTimerQueue(pid);
		if (pT) {
			pcb = pT->pcb;
		}
	}
	if (pcb) {
		pcb->priority = priority;
		releaseLock(USER);
		*errCode = ERR_SUCCESS;
		return;
	}
	RSQueueNode *p = searchInRSQueue(pid, ReadyQueue);
	if (p) {
		if (priority == p->pcb->priority) {
			releaseLock(USER);
			*errCode = ERR_SUCCESS;
			return;
		} else {
			RSQueueNode *pRS = p;
			if (priority > p->pcb->priority) {
				if (p->next) {
					while (pRS->next) {
						if (pRS->next->pcb->priority > priority) {
							break;
						}
						pRS = pRS->next;
					}
					p->next->previous = p->previous;
					if (p->previous) {
						p->previous->next = p->next;
					}
					p->next = pRS->next;
					p->previous = pRS;
				}
			} else {
				if (p->previous) {
					while (pRS->previous) {
						if (pRS->previous->pcb->priority >= priority) {
							break;
						}
						pRS = pRS->previous;
					}
					if (p->next) {
						p->next->previous = p->previous;
					}
					p->previous->next = p->next;
					p->next = pRS;
					p->previous = pRS->previous;
				}
			}
			p->pcb->priority = priority;
			releaseLock(USER);
			*errCode = ERR_SUCCESS;
			return;
		}
	} else {
		p = searchInRSQueue(pid, SuspendQueue);
		if (p) {
			p->pcb->priority = priority;
			*errCode = ERR_SUCCESS;
			return;
		} else {
			*errCode = ERR_BAD_PARAM;
			return;
		}
	}
}

void sendMessage(INT32 receiver, char *message, INT32 sendLength,
		INT32 *errCode) {
	if (sendLength > MESSAGE_LENGTH_UPPERBOUND || sendLength < 0
			|| strlen(message) > sendLength) {
		*errCode = ERR_BAD_PARAM;
		return;
	}
	MessageBox *box = NULL;
	if (receiver == -1) {
		if (!BroadcastMessageBox) {
			BroadcastMessageBox = (MessageBox *) calloc(1, sizeof(MessageBox));
			BroadcastMessageBox->next = BroadcastMessageBox->previous = NULL;
			BroadcastMessageBox->head = BroadcastMessageBox->tail = NULL;
			BroadcastMessageBox->size = 0;
		}
		box = BroadcastMessageBox;
		RSQueueNode *node = MessageSuspendQueue;
		while (node) {
			removeFromRSQueue(receiver, &MessageSuspendQueue, &node);
			addToReadyQueue(node);
			node = MessageSuspendQueue;
		}
		*errCode = ERR_SUCCESS;
		return;
	} else {
		getLock("INTERRUPT", USER);
		PCB *pcb = NULL;
		if (receiver == currentPCB->pid) {
			pcb = currentPCB;
		} else {
			TimerQueueNode *pT = searchInTimerQueue(receiver);
			if (pT) {
				pcb = pT->pcb;
			} else {
				INT32 i;
				RSQueueNode *p;
				for (i = 0; i < 3; i++) {
					if (i == 0) {
						p = searchInRSQueue(receiver, ReadyQueue);
					}
					if (i == 1) {
						p = searchInRSQueue(receiver, SuspendQueue);
					}
					if (i == 2) {
						p = searchInRSQueue(receiver, MessageSuspendQueue);
					}
					if (p) {
						pcb = p->pcb;
						if (i == 2) {
							removeFromRSQueue(receiver, &MessageSuspendQueue,
									&p);
							addToReadyQueue(p);
						}
						break;
					}
				}
			}
		}
		releaseLock(USER);
		if (pcb) {
			box = MessageBoxQueue;
			while (box) {
				if (box->receiver == receiver) {
					printf("box found\n");
					break;
				}
				box = box->next;
			}
			if (!box) {
				addMessageBox(receiver);
				box = MessageBoxQueue;
				printf("new box receiver: %d\n", receiver);
			}
		} else {
			*errCode = ERR_BAD_PARAM;
			return;
		}
	}
	if (box->size == MESSAGE_BOX_CAPACITY) {
		*errCode = ERR_BAD_PARAM;
		return;
	} else {
		Message *newMessage = (Message *) calloc(1, sizeof(Message));
		newMessage->next = NULL;
		newMessage->sendLength = sendLength;
		strcpy(newMessage->content, message);
		newMessage->sender = currentPCB->pid;
		if (!box->head) {
			box->head = box->tail = newMessage;
		} else {
			box->tail->next = newMessage;
			box->tail = newMessage;
		}
		box->size++;
		printf("new Message added, No. %d\n", box->size);
		*errCode = ERR_SUCCESS;
		return;
	}
}

void receiveMessage(INT32 sender, char *messageBuffer, INT32 receiveLength,
		INT32 *actualSendLength, INT32 *actualSender, INT32 *errCode) {
	while (true) {
		if (receiveLength < 0 || receiveLength > MESSAGE_LENGTH_UPPERBOUND) {
			*errCode = ERR_BAD_PARAM;
			return;
		}
		if (!currentPCB->messageBox) {
			currentPCB->messageBox = MessageBoxQueue;
			while (currentPCB->messageBox) {
				if (currentPCB->messageBox->receiver == currentPCB->pid) {
					break;
				}
				currentPCB->messageBox = currentPCB->messageBox->next;
			}
		}

		printf("MY BOX FOUND: %p\n", currentPCB->messageBox);
		fflush(stdout);

		PidNode *pidNode = PidEverExisted;
		if (sender != -1) {
			while (pidNode) {
				if (pidNode->pid == sender) {
					break;
				}
				pidNode = pidNode->next;
			}
			if (!pidNode) {
				printf("sender invalid: %d\n", sender);
				fflush(stdout);
				*errCode = ERR_BAD_PARAM;
				return;
			}
		}

		printf("sender valid: %d\n", sender);

		fflush(stdout);

		bool needsBroadcastMessageBox = false;

		if (currentPCB->messageBox) {
			Message * message = currentPCB->messageBox->head;
			Message * m = NULL;
			if (sender != -1) {
				while (message) {
					if (message->sender == sender) {
						break;
					}
					m = message;
					message = message->next;
				}
			}
			if (message) {
				printf("message found\n");
				fflush(stdout);
				if (message->sendLength <= receiveLength) {
					strcpy(messageBuffer, message->content);
					*actualSender = message->sender;
					*actualSendLength = message->sendLength;
					if (m) {
						m->next = message->next;
					} else {
						currentPCB->messageBox->head = message->next;
					}
					free(message);
					currentPCB->messageBox->size--;

					*errCode = ERR_SUCCESS;
					printf("valid message\n");
					fflush(stdout);
					printf("receive finished\n");
					fflush(stdout);
					return;
				} else {
					if (sender == -1) {
						needsBroadcastMessageBox = true;
					} else {
						*errCode = ERR_BAD_PARAM;
						return;
					}
				}
			} else {
				printf("no message in private box\n");
				fflush(stdout);
				if (sender == -1) {
					needsBroadcastMessageBox = true;
				} else {
					if (!pidNode->isAlive) {
						*errCode = ERR_BAD_PARAM;
						return;
					}
				}
			}
		} else {
			if (sender == -1) {
				needsBroadcastMessageBox = true;
				printf("looking at BroadcastMessageBox\n");
				fflush(stdout);
			} else {
				if (!pidNode->isAlive) {
					*errCode = ERR_BAD_PARAM;
					return;
				}
			}
		}

		if (needsBroadcastMessageBox) {
			printf("looking\n");
			fflush(stdout);
			if (BroadcastMessageBox) {
				Message *message = BroadcastMessageBox->head;
				if (message) {
					if (message->sendLength <= receiveLength) {
						strcpy(messageBuffer, message->content);
						*actualSender = message->sender;
						*actualSendLength = message->sendLength;
						BroadcastMessageBox->head = message->next;
						free(message);
						BroadcastMessageBox->size--;
						*errCode = ERR_SUCCESS;
						return;
					} else {
						printf("going to suspension\n");
						fflush(stdout);
					}
				} else {
					printf("No message\n");
					fflush(stdout);
				}
			}
		}

		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode *));
		node->pcb = currentPCB;
		node->pcb->isSuspended = true;
		node->next = NULL;
		node->previous = NULL;
		getLock("INTERRUPT", USER);
		addToSuspendQueue(node, &MessageSuspendQueue);
		dispatch("Receive", -1);
		Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
				(void *) (&currentPCB->context));
	}
}

void dispatch(char *op, INT32 time) {
	if (ReadyQueue) {
		currentPCB = ReadyQueue->pcb;
		printf("%s: Pid now to %d from ReadyQueue\n", op, currentPCB->pid);
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
				printf("%s: Pid now to %d from TimerQueue\n", op,
						currentPCB->pid);
				if (p != TimerQueue) {
					INT32 currentTime;
					if (time != -1) {
						currentTime = time;
					} else {
						CALL(MEM_READ(Z502ClockStatus, &currentTime));
					}
					startTimer(p->time - currentTime);
					printf("reset by user (%s): timer now to %d, pid = %d\n",
							op, p->time, p->pcb->pid);
				}
				removeFromTimerQueue(p->pcb->pid, &p);
				free(p);
			} else {
				Z502Halt();
			}
			interruptFinished = false;
			releaseLock(USER);
			printf("%s waiting for interrupt\n", op);
			Z502Idle();
			while (!interruptFinished)
				;
			printf("waiting finished\n");
		} else {
			Z502Halt();
		}
	}
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
	if (!(*p)) {
		*p = searchInTimerQueue(pid);
	}
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

	PCB *pP = currentPCB;
	if (strcmp(pP->process_name, process_name) == 0) {
		return ERR_BAD_PARAM;
	}

	RSQueueNode *p = NULL;
	INT32 i;
	for (i = 0; i < 3; i++) {
		if (i == 0) {
			p = ReadyQueue;
		}
		if (i == 1) {
			p = SuspendQueue;
		}
		if (i == 2) {
			p = MessageSuspendQueue;
		}
		while (p) {
			if (strcmp(p->pcb->process_name, process_name) == 0) {
				return ERR_BAD_PARAM;
			}
			p = p->next;
		}
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

void addMessageBox(INT32 receiver) {
	MessageBox *newBox = (MessageBox *) calloc(1, sizeof(MessageBox));
	newBox->receiver = receiver;
	newBox->head = newBox->tail = NULL;
	newBox->size = 0;
	newBox->previous = NULL;
	if (MessageBoxQueue) {
		MessageBoxQueue->previous = newBox;
	}
	newBox->next = MessageBoxQueue;
	MessageBoxQueue = newBox;
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
	if (!(*p)) {
		*p = searchInRSQueue(pid, *queueHead);
	}
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

void addNewPid( pid) {
	PidNode *node = PidEverExisted;
	while (node) {
		if (node->pid == pid) {
			break;
		}
		node = node->next;
	}
	if (!node) {
		PidNode *newNode = (PidNode *) calloc(1, sizeof(PidNode));
		newNode->pid = pid;
		newNode->isAlive = true;
		newNode->next = NULL;
		newNode->next = PidEverExisted;
		PidEverExisted = newNode;
	} else {
		if (!node->isAlive) {
			node->isAlive = true;
		}
	}
}

void killPid(INT32 pid) {
	PidNode *p = PidEverExisted;
	while (p) {
		if (p->pid == pid && p->isAlive) {
			p->isAlive = false;
			break;
		}
		p = p->next;
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
