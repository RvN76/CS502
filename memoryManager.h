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

typedef struct sArea {
	char tag[32];
	INT32 startingFrame;
	INT32 length;
	INT32 numberOfPreviousSharers;
	struct sArea *next;
} SharedArea;

FrameAssignmentNode *InvertedPageTable[PHYS_MEM_PGS ];

UINT16 RoundsUnreferenced[PHYS_MEM_PGS ];

INT32 MostUnluckyVictim;

SharedArea *SharedAreas;

INT32 memoryPrinter_DisplayGranularity;

INT32 memoryPrinter_Count;

void allocateFrame(INT32);

void getThePageFromDisk(INT32);

INT32 chooseAndReset();

void defineSharedArea(INT32, INT32, char *, INT32 *, INT32 *);

void memoryPrinter();

#endif /* MEMORYMANAGER_H_ */
