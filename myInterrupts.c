/*
 * MyInterrupts.c
 *
 *  Created on: 2014��9��16��
 *      Author: Dongyun
 */

#include "myInterrupts.h"
#include "processControl.h"

void timerInterrupt() {
	wakeUpProcess();
}
