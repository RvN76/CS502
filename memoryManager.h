/*
 * memoryManager.h
 *
 *  Created on: 2014Äê11ÔÂ4ÈÕ
 *      Author: Dongyun
 */

#ifndef MEMORYMANAGER_H_
#define MEMORYMANAGER_H_

#include "global.h"
#include "Z502.h"

typedef struct ad {
	INT32 pid;
	UINT16 frame;
	struct ad *next;
} FrameAssignmentNode;

FrameAssignmentNode *addressList;

void initializeSlot(INT32);

#endif /* MEMORYMANAGER_H_ */
