/*
 * MyInterrupts.c
 *
 *  Created on: 2014Äê9ÔÂ16ÈÕ
 *      Author: Dongyun
 */

#include "myInterrupts.h"
#include "processControl.h"
#include "stdio.h"

void timerInterrupt() {
	sprintf(operation, "wakeUpProcess");
	getLock("USER", INTERRUPT);
//	tryingToHandle[INTERRUPT] = true;
//	prioritiveProcess = USER;
//	int c = 0;
//	while(tryingToHandle[USER] == true && prioritiveProcess == USER){
//		if(c == 0){
//			printf("blocked by USER, wait\n");
//			c = 1;
//		}
//		continue;
//	}
	wakeUpProcesses(false, NULL);
	releaseLock(INTERRUPT);
//	tryingToHandle[INTERRUPT] = false;
}
