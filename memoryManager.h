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

#define PTBL_ON_DISK_BIT	0x1000

typedef struct {
	INT32 pid;
	UINT16 *pageTable;
	INT16 page;
} FrameAssignmentNode;

FrameAssignmentNode *InvertedPageTable[PHYS_MEM_PGS ];

void allocatePage(INT32);

void getThePageFromDisk(INT32);

INT32 chooseAndReset();

void selectDiskAndSector(INT32, INT32 *, INT32 *);

#endif /* MEMORYMANAGER_H_ */
