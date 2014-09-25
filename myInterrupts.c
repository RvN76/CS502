/*
 * MyInterrupts.c
 *
 *  Created on: 2014Äê9ÔÂ16ÈÕ
 *      Author: Dongyun
 */

#include "myInterrupts.h"
#include "processControl.h"

void timerInterrupt() {
	wakeUpProcess();
}
