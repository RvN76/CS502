/*
 * mySVC.c
 *
 *  Created on: 2014Äê10ÔÂ15ÈÕ
 *      Author: Dongyun
 */

#include "mySVC.h"
#include "memoryManager.h"
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
DiskQueueNode *DiskQueue = NULL;

MessageBox *MessageBoxQueue = NULL;
MessageBox *BroadcastMessageBox = NULL;

PidNode *PidEverExisted = NULL;

INT32 LockingResult[5] = { 0, 0, 0, 0, 0 };

INT32 DiskOccupation[2] = { -1, -1 };

bool InterruptFinished = true;

void osCreateProcess(void *starting_address) {
	void *next_context;
	Z502MakeContext(&next_context, starting_address, USER_MODE);
	PCB *p = (PCB *) calloc(1, sizeof(PCB));
	p->pid = pidToAssign;
	strcpy(p->process_name, "osProcess");
	p->priority = 1;
	p->suspended = NOT_SUSPENDED;
	pidToAssign++;
	p->context = (Z502CONTEXT *) next_context;
	p->messageBox = NULL;
	addNewPid(p->pid);
	currentPCB = p;
	numOfProcesses++;
//	Switch the context and begin the test
	Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE, &next_context);
}

void createProcess(char *process_name, void *entry, INT32 priority,
		INT32 *pidToReturn, INT32 *errCode) {
	INT32 checkResult = checkProcessParams(process_name, entry, priority);
//	If the params are all valid, create a new PCB and add it to ReadyQueue
	if (checkResult == ERR_SUCCESS) {
		PCB *p = (PCB *) calloc(1, sizeof(PCB));
		strcpy(p->process_name, process_name);
		void *next_context;
		Z502MakeContext(&next_context, entry, USER_MODE);
		p->context = (Z502CONTEXT *) next_context;
		p->pid = pidToAssign;
		p->priority = priority;
		p->suspended = NOT_SUSPENDED;
		p->messageBox = NULL;
		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = p;
		node->previous = NULL;
		node->next = NULL;
		getMyLock(READYQUEUELOCK);
		addToReadyQueue(node);
		releaseMyLock(READYQUEUELOCK);
		addNewPid(p->pid);
		numOfProcesses++;
		*pidToReturn = p->pid;
		pidToAssign++;
	}
//	releaseLock(USER);
	*errCode = checkResult;
}

void sleepProcess(INT32 timeToSleep) {
	if (timeToSleep <= 6) {
		timeToSleep = 0;
	} else {
		timeToSleep -= 6;
	}
	INT32 startTime, absoluteTime;
//	add the current process to TimerQueue
	CALL(MEM_READ(Z502ClockStatus, &startTime));
	absoluteTime = startTime + timeToSleep;
	TimerQueueNode *node = (TimerQueueNode *) calloc(1, sizeof(TimerQueueNode));
	node->pcb = currentPCB;
	node->time = absoluteTime;
	node->next = NULL;
	node->previous = NULL;
	getMyLock(TIMERQUEUELOCK);

	addToTimerQueue(node);

	if (TimerQueue == node) {
		startTimer(timeToSleep);
//		printf("reset by user (sleep): timer now to %d, pid = %d\n", node->time,
//				node->pcb->pid);
	}
	releaseMyLock(TIMERQUEUELOCK);
//	decide what to run next and switch
	dispatch("Sleep", startTime);
	Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
			(void *) (&currentPCB->context));
}

void suspendProcess(INT32 pid, INT32 *errCode) {
	if (pid == -1 || pid == currentPCB->pid) {
		*errCode = ERR_BAD_PARAM;
		return;
	} else {
		RSQueueNode *p = NULL;
//		check queues for the pid, move to SuspendQueue or simply label as "Suspended"
		getMyLock(READYQUEUELOCK);
		getMyLock(TIMERQUEUELOCK);
		getMyLock(SUSPENDQUEUELOCK);
		removeFromRSQueue(pid, &ReadyQueue, &p);
		if (p) {
			releaseMyLock(TIMERQUEUELOCK);
			releaseMyLock(READYQUEUELOCK);
			p->pcb->suspended = SUSPENDED;
			addToSuspendQueue(p);
			releaseMyLock(SUSPENDQUEUELOCK);
//			printf("Pid %d in ReadyQueue is suspended\n", pid);
			*errCode = ERR_SUCCESS;
			schedulerPrinter(currentPCB->pid, currentPCB->pid, "Suspend", -1);
			return;
		} else {
			releaseMyLock(READYQUEUELOCK);
			TimerQueueNode *q = searchInTimerQueue(pid);
			if (q) {
				releaseMyLock(SUSPENDQUEUELOCK);
				if (q->pcb->suspended != SUSPENDED) {
					q->pcb->suspended = SUSPENDED;
					releaseMyLock(TIMERQUEUELOCK);
//					printf("Pid %d in TimerQueue is suspended\n", pid);
					*errCode = ERR_SUCCESS;
					schedulerPrinter(currentPCB->pid, currentPCB->pid,
							"Suspend", -1);
				} else {
					releaseMyLock(TIMERQUEUELOCK);
					*errCode = ERR_BAD_PARAM;
				}
				return;
			} else {
				releaseMyLock(TIMERQUEUELOCK);
				p = searchInRSQueue(pid, SuspendQueue);
				if (p && p->pcb->suspended != SUSPENDED) {
					p->pcb->suspended = SUSPENDED;
					releaseMyLock(SUSPENDQUEUELOCK);
					*errCode = ERR_SUCCESS;
					schedulerPrinter(currentPCB->pid, currentPCB->pid,
							"Suspend", -1);
//					printf("Pid %d in SuspendQueue is suspended\n", pid);
				} else {
					releaseMyLock(SUSPENDQUEUELOCK);
					*errCode = ERR_BAD_PARAM;
				}
				return;
			}
		}
	}
}

void resumeProcess(INT32 pid, INT32 *errCode) {
	RSQueueNode *p = NULL;
// check queues for pid, resume or label as "Not suspended"
	getMyLock(READYQUEUELOCK);
	getMyLock(TIMERQUEUELOCK);
	getMyLock(SUSPENDQUEUELOCK);
	removeFromRSQueue(pid, &SuspendQueue, &p);
	if (p) {
		releaseMyLock(SUSPENDQUEUELOCK);
		releaseMyLock(TIMERQUEUELOCK);
		p->pcb->suspended = NOT_SUSPENDED;
		addToReadyQueue(p);
		releaseMyLock(READYQUEUELOCK);
		*errCode = ERR_SUCCESS;
		schedulerPrinter(currentPCB->pid, currentPCB->pid, "Resume", -1);
	} else {
		releaseMyLock(SUSPENDQUEUELOCK);
		releaseMyLock(READYQUEUELOCK);
		TimerQueueNode *q = searchInTimerQueue(pid);
		if (q && q->pcb->suspended != NOT_SUSPENDED) {
			q->pcb->suspended = NOT_SUSPENDED;
			releaseMyLock(TIMERQUEUELOCK);
			*errCode = ERR_SUCCESS;
			schedulerPrinter(currentPCB->pid, currentPCB->pid, "Resume", -1);
		} else {
			releaseMyLock(TIMERQUEUELOCK);
			*errCode = ERR_BAD_PARAM;
		}
	}
	return;
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
		INT32 tPid = currentPCB->pid;
//		decide what to run next
		dispatch("Term", -1);
		killPid(tPid);
		removeMessageBox(tPid);
		Z502SwitchContext(SWITCH_CONTEXT_KILL_MODE,
				(void *) (&currentPCB->context));
		break;
	default: {
//		check the queues for pid and remove it
		RSQueueNode *p = NULL;
		getMyLock(READYQUEUELOCK);
		getMyLock(TIMERQUEUELOCK);
		getMyLock(SUSPENDQUEUELOCK);
		int i;
		for (i = 0; i < 2; i++) {
			if (i == 0) {
				removeFromRSQueue(pidToTerminate, &ReadyQueue, &p);
				releaseMyLock(READYQUEUELOCK);
			}
			if (i == 1) {
				removeFromRSQueue(pidToTerminate, &SuspendQueue, &p);
				releaseMyLock(SUSPENDQUEUELOCK);
			}
			if (p) {
				releaseMyLock(TIMERQUEUELOCK);
				if (i == 0) {
					releaseMyLock(SUSPENDQUEUELOCK);
				}
				break;
			}
		}
		if (p) {
			free(p);
			killPid(pidToTerminate);
			removeMessageBox(pidToTerminate);
			numOfProcesses--;
			*errCode = ERR_SUCCESS;
		} else {
			TimerQueueNode *q = NULL;
			removeFromTimerQueue(pidToTerminate, &q);
			releaseMyLock(TIMERQUEUELOCK);
			if (q) {
				free(q);
				killPid(pidToTerminate);
				removeMessageBox(pidToTerminate);
				numOfProcesses--;
				*errCode = ERR_SUCCESS;
			} else {
				*errCode = ERR_BAD_PARAM;
			}
		}
		break;
	}
	}
}

void getProcessID(char *process_name, INT32 *process_id, INT32 *errCode) {
	if (strcmp(process_name, "") == 0
			|| strcmp(process_name, currentPCB->process_name) == 0) {
		*process_id = currentPCB->pid;
		*errCode = ERR_SUCCESS;
		return;
	}
	getMyLock(READYQUEUELOCK);
	getMyLock(TIMERQUEUELOCK);
	getMyLock(SUSPENDQUEUELOCK);
	RSQueueNode *p = NULL;
	int i;
	for (i = 0; i < 2; i++) {
		if (i == 0) {
			p = ReadyQueue;
		}
		if (i == 1) {
			releaseMyLock(READYQUEUELOCK);
			p = SuspendQueue;
		}
		while (p) {
			if (strcmp(p->pcb->process_name, process_name) == 0) {
				*process_id = p->pcb->pid;
				releaseMyLock(SUSPENDQUEUELOCK);
				releaseMyLock(TIMERQUEUELOCK);
				if (i == 0) {
					releaseMyLock(READYQUEUELOCK);
				}
				*errCode = ERR_SUCCESS;
				return;
			}
			p = p->next;
		}
	}

	releaseMyLock(SUSPENDQUEUELOCK);
	releaseMyLock(READYQUEUELOCK);
	TimerQueueNode *q = TimerQueue;
	while (q) {
		if (strcmp(q->pcb->process_name, process_name) == 0) {
			*process_id = q->pcb->pid;
			releaseMyLock(TIMERQUEUELOCK);
			*errCode = ERR_SUCCESS;
			return;
		}
		q = q->next;
	}
	releaseMyLock(TIMERQUEUELOCK);
	*errCode = ERR_BAD_PARAM;
	return;
}

void changePriority(INT32 pid, INT32 priority, INT32 *errCode) {
	if (priority <= 0 || priority > LEGAL_PRIORITY_UPPER_BOUND) {
		*errCode = ERR_BAD_PARAM;
		return;
	}

	if (pid == currentPCB->pid || pid == -1) {
		currentPCB->priority = priority;
		*errCode = ERR_SUCCESS;
		return;
	}

	getMyLock(READYQUEUELOCK);
	getMyLock(TIMERQUEUELOCK);
	getMyLock(SUSPENDQUEUELOCK);

	RSQueueNode *p = searchInRSQueue(pid, ReadyQueue);

	if (p) {
		if (priority != p->pcb->priority) {
			removeFromRSQueue(p->pcb->pid, &ReadyQueue, &p);
			p->pcb->priority = priority;
			addToReadyQueue(p);
		}
		releaseMyLock(SUSPENDQUEUELOCK);
		releaseMyLock(TIMERQUEUELOCK);
		releaseMyLock(READYQUEUELOCK);
		*errCode = ERR_SUCCESS;
		return;
	}

	releaseMyLock(READYQUEUELOCK);

	TimerQueueNode *pT = searchInTimerQueue(pid);
	if (pT) {
		pT->pcb->priority = priority;
		releaseMyLock(SUSPENDQUEUELOCK);
		releaseMyLock(TIMERQUEUELOCK);
		*errCode = ERR_SUCCESS;
		return;
	}

	releaseMyLock(TIMERQUEUELOCK);

	p = searchInRSQueue(pid, SuspendQueue);
	if (p) {
		p->pcb->priority = priority;
		releaseMyLock(SUSPENDQUEUELOCK);
		*errCode = ERR_SUCCESS;
	} else {
		releaseMyLock(SUSPENDQUEUELOCK);
		*errCode = ERR_BAD_PARAM;
	}
	return;
}

void sendMessage(INT32 recipient, char *message, INT32 sendLength,
		INT32 *errCode) {
//	send length and message length validity
	if (sendLength > MESSAGE_LENGTH_UPPERBOUND || sendLength < 0
			|| strlen(message) > sendLength) {
		*errCode = ERR_BAD_PARAM;
		return;
	}
	MessageBox *box = NULL;
	if (recipient == -1) {
		if (!BroadcastMessageBox) {
			BroadcastMessageBox = (MessageBox *) calloc(1, sizeof(MessageBox));
			BroadcastMessageBox->next = NULL;
			BroadcastMessageBox->head = BroadcastMessageBox->tail =
			NULL;
			BroadcastMessageBox->size = 0;
		}
		box = BroadcastMessageBox;
		getMyLock(READYQUEUELOCK);
		getMyLock(SUSPENDQUEUELOCK);
		RSQueueNode *node = SuspendQueue;
//		wake the recipients
		while (node) {
			if (node->pcb->suspended == WAITING_FOR_MESSAGE) {
				removeFromRSQueue(recipient, &SuspendQueue, &node);
				node->pcb->suspended = NOT_SUSPENDED;
				addToReadyQueue(node);
			}
			node = node->next;
		}
		releaseMyLock(SUSPENDQUEUELOCK);
		releaseMyLock(READYQUEUELOCK);
	} else {
		PCB *pcb = NULL;
		if (recipient == currentPCB->pid) {
			pcb = currentPCB;
		} else {
//			recipient validity check and wake
			getMyLock(READYQUEUELOCK);
			getMyLock(SUSPENDQUEUELOCK);
			getMyLock(TIMERQUEUELOCK);

			INT32 i;
			RSQueueNode *p = NULL;
			for (i = 0; i < 2; i++) {
				if (i == 0) {
					p = searchInRSQueue(recipient, SuspendQueue);
				}
				if (i == 1) {
					releaseMyLock(SUSPENDQUEUELOCK);
					p = searchInRSQueue(recipient, ReadyQueue);
				}
				if (p) {
					if (p) {
						if (p->pcb->suspended == WAITING_FOR_MESSAGE) {
							removeFromRSQueue(recipient, &SuspendQueue, &p);
							releaseMyLock(SUSPENDQUEUELOCK);
							p->pcb->suspended = NOT_SUSPENDED;
							addToReadyQueue(p);
						} else {
							releaseMyLock(SUSPENDQUEUELOCK);
						}
					}
					pcb = p->pcb;
					releaseMyLock(READYQUEUELOCK);
					break;
				} else if (i == 1) {
					releaseMyLock(READYQUEUELOCK);
				}
			}
			if (!p) {
				TimerQueueNode *pT = searchInTimerQueue(recipient);
				if (pT) {
					pcb = pT->pcb;
				}
			}
			releaseMyLock(TIMERQUEUELOCK);
		}
//		locate the box
		if (pcb) {
			box = MessageBoxQueue;
			while (box) {
				if (box->recipient == recipient) {
//					printf("box found\n");
					break;
				}
				box = box->next;
			}
			if (!box) {
				addMessageBox(recipient);
				box = MessageBoxQueue;
//				printf("new box receiver: %d\n", recipient);
			}
		} else {
			*errCode = ERR_BAD_PARAM;
			return;
		}
	}
//	check box size and copy message
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
//		printf("new Message added, No. %d\n", box->size);
//		fflush(stdout);
		*errCode = ERR_SUCCESS;
		return;
	}
}

void receiveMessage(INT32 sender, char *messageBuffer, INT32 receiveLength,
		INT32 *actualSendLength, INT32 *actualSender, INT32 *errCode) {
	if (receiveLength < 0 || receiveLength > MESSAGE_LENGTH_UPPERBOUND) {
		*errCode = ERR_BAD_PARAM;
		return;
	}
	while (true) {
//		find its own box
		if (!currentPCB->messageBox) {
			currentPCB->messageBox = MessageBoxQueue;
			while (currentPCB->messageBox) {
				if (currentPCB->messageBox->recipient == currentPCB->pid) {
					break;
				}
				currentPCB->messageBox = currentPCB->messageBox->next;
			}
		}

//		Check existence of sender
		PidNode *pidNode = PidEverExisted;
		if (sender != -1) {
			while (pidNode) {
				if (pidNode->pid == sender) {
					break;
				}
				pidNode = pidNode->next;
			}
			if (!pidNode) {
				*errCode = ERR_BAD_PARAM;
				return;
			}
		}

		bool needsBroadcastMessageBox = false;
// find message in private box
//		if no message and sender is -1, turn to BroadcastMessageBox
		if (currentPCB->messageBox) {
			Message *message = currentPCB->messageBox->head;
			Message *m = NULL;
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
				if (message->sendLength <= receiveLength) {
					strcpy(messageBuffer, message->content);
					*actualSender = message->sender;
					*actualSendLength = message->sendLength;
					if (m) {
						m->next = message->next;
					} else {
						currentPCB->messageBox->head = message->next;
						if (!currentPCB->messageBox->head) {
							currentPCB->messageBox->tail = NULL;
						}
					}
					free(message);
					currentPCB->messageBox->size--;
					*errCode = ERR_SUCCESS;
				} else {
					*errCode = ERR_BAD_PARAM;
				}
				return;
			} else {
				if (sender == -1) {
					needsBroadcastMessageBox = true;
				} else {
					if (!(pidNode->isAlive)) {
						*errCode = ERR_BAD_PARAM;
						return;
					}
				}
			}
		} else {
			if (sender == -1) {
				needsBroadcastMessageBox = true;
//				printf("looking at BroadcastMessageBox\n");
			} else {
				if (!(pidNode->isAlive)) {
					*errCode = ERR_BAD_PARAM;
					return;
				}
			}
		}

		if (needsBroadcastMessageBox) {
//			printf("looking\n");
			if (BroadcastMessageBox) {
				Message *message = BroadcastMessageBox->head;
				if (message) {
					if (message->sendLength <= receiveLength) {
						strcpy(messageBuffer, message->content);
						*actualSender = message->sender;
						*actualSendLength = message->sendLength;
						BroadcastMessageBox->head = message->next;
						if (!BroadcastMessageBox->head) {
							BroadcastMessageBox->tail = NULL;
						}
						free(message);
						BroadcastMessageBox->size--;
						*errCode = ERR_SUCCESS;
						return;
					} else {
						*errCode = ERR_BAD_PARAM;
						return;
					}
				}
			}
		}
//going to suspension, and starts over once it runs again
		RSQueueNode *node = (RSQueueNode *) calloc(1, sizeof(RSQueueNode));
		node->pcb = currentPCB;
		node->pcb->suspended = WAITING_FOR_MESSAGE;
		node->next = NULL;
		node->previous = NULL;
		getMyLock(SUSPENDQUEUELOCK);
		addToSuspendQueue(node);
		releaseMyLock(SUSPENDQUEUELOCK);
		dispatch("Receive", -1);
		Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
				(void *) (&currentPCB->context));
	}
}

void requestForDisk(INT16 action, INT32 disk_id, INT32 sector, char *data) {
	char op[7];
	DiskQueueNode *node = (DiskQueueNode *) calloc(1, sizeof(DiskQueueNode));
	node->next = NULL;
	if (action == SYSNUM_DISK_READ) {
		sprintf(op, "Disk_R");
		node->action = READ;
	} else if (action == SYSNUM_DISK_WRITE) {
		sprintf(op, "Disk_W");
		node->action = WRITE;
	}
	node->disk_id = disk_id;
	node->sector = sector;
	node->data = data;
	node->pcb = currentPCB;
	getMyLock(DISKQUEUELOCK);
	addToDiskQueue(node);
//	releaseMyLock(DISKQUEUELOCK);
	if (DiskOccupation[disk_id - 1] == -1) {
		DiskOccupation[disk_id - 1] = currentPCB->pid;
		MEM_WRITE(Z502DiskSetID, &node->disk_id);
		MEM_WRITE(Z502DiskSetSector, &node->sector);
		MEM_WRITE(Z502DiskSetBuffer, (INT32 * )node->data);
		MEM_WRITE(Z502DiskSetAction, (INT32 * )(&node->action));
		INT32 Start = 0;
		MEM_WRITE(Z502DiskStart, &Start);
	}
	releaseMyLock(DISKQUEUELOCK);
	dispatch("Disk_R", -1);
	Z502SwitchContext(SWITCH_CONTEXT_SAVE_MODE,
			(void *) (&currentPCB->context));
}

void dispatch(char *op, INT32 time) {
//	PCB *pcb = NULL;
	getMyLock(READYQUEUELOCK);
//	getMyLock(TIMERQUEUELOCK);
//If ReadyQueue is not null, pick the first one
	if (ReadyQueue) {
//		printf("%s: Pid now to %d from ReadyQueue\n", op, currentPCB->pid);
		RSQueueNode *p = ReadyQueue;
//		pcb = p->pcb;
		ReadyQueue = ReadyQueue->next;
		if (ReadyQueue) {
			ReadyQueue->previous = NULL;
		}
		p->previous = p->next = NULL;
//		releaseMyLock(TIMERQUEUELOCK);
		releaseMyLock(READYQUEUELOCK);
		schedulerPrinter(currentPCB->pid, p->pcb->pid, op, time);
		currentPCB = p->pcb;
		free(p);
	} else {
//	if the timer queue is  not null, pick the first "NOT_SUSPEND one"
//	and waits until it appears in the ReadyQueue
		getMyLock(TIMERQUEUELOCK);
		getMyLock(DISKQUEUELOCK);
		if (TimerQueue || DiskQueue) {
//			TimerQueueNode *p = TimerQueue;
			RSQueueNode *q = NULL;
////			RSQueueNode *q = ReadyQueue;
//			while (p) {
//				if (p->pcb->suspended == NOT_SUSPENDED) {
//					pcb = p->pcb;
//					break;
//				}
//			}
//			while ((q = searchInRSQueue(pcb->pid, ReadyQueue)) == NULL) {
//				releaseMyLock(TIMERQUEUELOCK);
//				releaseMyLock(READYQUEUELOCK);
//				InterruptFinished = false;
//				Z502Idle();
////			keep waiting until the interrupt handler finishes
//				while (!InterruptFinished)
//					;
//				getMyLock(READYQUEUELOCK);
//				getMyLock(TIMERQUEUELOCK);
//			}
			while ((TimerQueue || DiskQueue) && (!ReadyQueue)) {
				InterruptFinished = false;
				releaseMyLock(DISKQUEUELOCK);
				releaseMyLock(TIMERQUEUELOCK);
				releaseMyLock(READYQUEUELOCK);
				Z502Idle();
				//			keep waiting until the interrupt handler finishes
				while (!InterruptFinished)
					;
				getMyLock(READYQUEUELOCK);
				getMyLock(TIMERQUEUELOCK);
				getMyLock(DISKQUEUELOCK);
			}
			releaseMyLock(DISKQUEUELOCK);
			releaseMyLock(TIMERQUEUELOCK);
//			removeFromRSQueue(pcb->pid, &ReadyQueue, &q);
			removeFromRSQueue(ReadyQueue->pcb->pid, &ReadyQueue, &q);
			releaseMyLock(READYQUEUELOCK);
			schedulerPrinter(currentPCB->pid, q->pcb->pid, op, time);
			currentPCB = q->pcb;
			free(q);
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

	getMyLock(READYQUEUELOCK);
	getMyLock(TIMERQUEUELOCK);
	getMyLock(SUSPENDQUEUELOCK);
	RSQueueNode *p = NULL;
	INT32 i;
	for (i = 0; i < 2; i++) {
		if (i == 0) {
			p = ReadyQueue;
		}
		if (i == 1) {
			releaseMyLock(READYQUEUELOCK);
			p = SuspendQueue;
		}
		while (p) {
			if (strcmp(p->pcb->process_name, process_name) == 0) {
				if (i == 0) {
					releaseMyLock(READYQUEUELOCK);
				}
				releaseMyLock(SUSPENDQUEUELOCK);
				releaseMyLock(TIMERQUEUELOCK);
				return ERR_BAD_PARAM;
			}
			p = p->next;
		}
	}

	releaseMyLock(SUSPENDQUEUELOCK);
	TimerQueueNode *pT = TimerQueue;
	while (pT) {
		if (strcmp(pT->pcb->process_name, process_name) == 0) {
			releaseMyLock(TIMERQUEUELOCK);
			return ERR_BAD_PARAM;
		}
		pT = pT->next;
	}
	releaseMyLock(TIMERQUEUELOCK);
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

void addToSuspendQueue(RSQueueNode *node) {
	if (!SuspendQueue) {
		SuspendQueue = node;
		return;
	}
	node->next = SuspendQueue;
	SuspendQueue->previous = node;
	SuspendQueue = node;
}

void addToDiskQueue(DiskQueueNode *node) {
	if (!DiskQueue) {
		DiskQueue = node;
		return;
	}
	DiskQueueNode *p = DiskQueue;
	while (p->next) {
		p = p->next;
	}
	p->next = node;
	node->previous = p;
}

void addMessageBox(INT32 receiver) {
	MessageBox *newBox = (MessageBox *) calloc(1, sizeof(MessageBox));
	newBox->recipient = receiver;
	newBox->head = newBox->tail = NULL;
	newBox->size = 0;
	newBox->next = MessageBoxQueue;
	MessageBoxQueue = newBox;
}

void removeMessageBox(INT32 receiver) {
	MessageBox *box = MessageBoxQueue, *b;
	while (box) {
		if (box->recipient == receiver) {
			break;
		}
		b = box;
		box = box->next;
	}
	if (box) {
		if (box == MessageBoxQueue) {
			MessageBoxQueue = box->next;
		} else {
			b->next = box->next;
		}
		free(box);
	}
	return;
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

void getMyLock(INT32 type) {
	READ_MODIFY((INT32)(MEMORY_INTERLOCK_BASE + 16 + type), 1, (BOOL )1,
			&LockingResult[type]);
}

void releaseMyLock(INT32 type) {
	READ_MODIFY((INT32)(MEMORY_INTERLOCK_BASE + 16 + type), 0, (BOOL )1,
			&LockingResult[type]);
}

void schedulerPrinter(INT32 currentPid, INT32 targetPid, char *action,
		INT32 time) {
	INT32 currentTime;
	TimerQueueNode *pT = NULL;
	RSQueueNode *pR = NULL, *pS = NULL;
	DiskQueueNode *pD = NULL;
	PidNode *pP = NULL;

	getMyLock(PRINTERLOCK);
	if (time == -1) {
		MEM_READ(Z502ClockStatus, &currentTime);
	} else {
		currentTime = time;
	}

	SP_setup(SP_TIME_MODE, currentTime);
	SP_setup_action(SP_ACTION_MODE, action);
	SP_setup(SP_RUNNING_MODE, currentPid);
	SP_setup(SP_TARGET_MODE, targetPid);

	getMyLock(READYQUEUELOCK);
	getMyLock(TIMERQUEUELOCK);
	getMyLock(DISKQUEUELOCK);
	getMyLock(SUSPENDQUEUELOCK);

//	print the ReadyQueue
	pR = ReadyQueue;
	while (pR) {
		SP_setup(SP_READY_MODE, pR->pcb->pid);
		pR = pR->next;
	}
	releaseMyLock(READYQUEUELOCK);

//	print the TimerQueue
	pT = TimerQueue;
	while (pT) {
		SP_setup(SP_TIMER_SUSPENDED_MODE, pT->pcb->pid);
		pT = pT->next;
	}
	releaseMyLock(TIMERQUEUELOCK);

//	print the DiskQueue
	pD = DiskQueue;
	while (pD) {
		SP_setup(SP_DISK_SUSPENDED_MODE, pD->pcb->pid);
		pD = pD->next;
	}
	releaseMyLock(DISKQUEUELOCK);

//	print the SuspendQueue
	pS = SuspendQueue;
	while (pS) {
		SP_setup(SP_PROCESS_SUSPENDED_MODE, pS->pcb->pid);
		pS = pS->next;
	}

	releaseMyLock(SUSPENDQUEUELOCK);

//  print the dead pids
	pP = PidEverExisted;
	while (pP) {
		if (!pP->isAlive) {
			SP_setup(SP_TERMINATED_MODE, pP->pid);
		}
		pP = pP->next;
	}

	SP_print_line();
	releaseMyLock(PRINTERLOCK);
}

