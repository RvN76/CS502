/*
 * MyInterrupts.c
 *
 *  Created on: 2014Äê9ÔÂ16ÈÕ
 *      Author: Dongyun
 */

#include "myInterrupts.h"
#include "mySVC.h"
#include "stdio.h"

void timerInterrupt() {
//	sprintf(operation, "wakeUpProcess");
//	getLock("USER", INTERRUPT);
	wakeUpProcesses(false, NULL);
//	interruptFinished = true;
//	releaseLock(INTERRUPT);
}
