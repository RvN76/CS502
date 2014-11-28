/*
 * memoryManager.c
 *
 *  Created on: 2014Äê11ÔÂ4ÈÕ
 *      Author: Dongyun
 */

#include "global.h"
#include "syscalls.h"
#include "protos.h"
#include "memoryManager.h"
#include "mySVC.h"

#include "string.h"
#include "stdlib.h"

FrameAssignmentNode *InvertedPageTable[PHYS_MEM_PGS];

void allocatePage(INT32 page) {
	while (true) {
		INT32 i;
		for (i = 0; i < PHYS_MEM_PGS ; i++) {
			if (!(InvertedPageTable[i])) {
				FrameAssignmentNode *node = (FrameAssignmentNode *) calloc(1,
						sizeof(FrameAssignmentNode));
				node->pid = currentPCB->pid;
				node->pageTable = currentPCB->pageTable;
				node->page = page;
				InvertedPageTable[i] = node;
				currentPCB->pageTable[page] = i | PTBL_VALID_BIT;
				return;
			}
		}
		INT32 victim = chooseAndReset();
		INT32 disk = (InvertedPageTable[victim]->pid + 1) % MAX_NUMBER_OF_DISKS;
		INT32 sector = InvertedPageTable[victim]->page;
//		INT32 disk = ((InvertedPageTable[victim]->pid * 1024
//				+ InvertedPageTable[victim]->page) / NUM_LOGICAL_SECTORS ) + 1;
//		INT32 sector = (InvertedPageTable[victim]->pid * 1024
//				+ InvertedPageTable[victim]->page)
//				- (disk - 1) * NUM_LOGICAL_SECTORS;
		char *buffer = (char *) calloc(PGSIZE, sizeof(char));
		Z502ReadPhysicalMemory(victim, buffer);
		InvertedPageTable[victim]->pageTable[InvertedPageTable[victim]->page] =
		PTBL_ON_DISK_BIT;
		free(InvertedPageTable[victim]);
		InvertedPageTable[victim] = NULL;
		requestForDisk(SYSNUM_DISK_WRITE, disk, sector, buffer);
//	FrameAssignmentNode *p = InvertedPageTable[victim];
//	free(buffer);
//	buffer = (char *) calloc(PGSIZE, sizeof(char));
//	Z502WritePhysicalMemory(victim, buffer);
//	p->pid = currentPCB->pid;
//	p->pageTable = currentPCB->pageTable;
//	p->page = page;
//	currentPCB->pageTable[page] = victim | PTBL_VALID_BIT;
	}
}

void getThePageFromDisk(INT32 page) {
	char *buffer = (char *) calloc(PGSIZE, sizeof(char));
	INT32 disk = (currentPCB->pid + 1) % MAX_NUMBER_OF_DISKS;
	INT32 sector = page;
//	INT32 disk = ((currentPCB->pid * 1024 + page) / NUM_LOGICAL_SECTORS ) + 1;
//	INT32 sector = (currentPCB->pid * 1024 + page)
//			- (disk - 1) * NUM_LOGICAL_SECTORS;
	requestForDisk(SYSNUM_DISK_READ, disk, sector, buffer);
	currentPCB->pageTable[page] = 0;
	allocatePage(page);
	Z502WritePhysicalMemory(currentPCB->pageTable[page] & PTBL_PHYS_PG_NO,
			buffer);
}

INT32 chooseAndReset() {
	INT32 victim = -1;
	INT32 i;
	for (i = 0; i < PHYS_MEM_PGS ; i++) {
		if ((InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page]
				& PTBL_REFERENCED_BIT) == 0) {
			if (victim == -1) {
				victim = i;
			}
			InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page] &=
					(PTBL_VALID_BIT | PTBL_MODIFIED_BIT | PTBL_PHYS_PG_NO);
		}
	}
	if (victim == -1) {
		for (i = 0; i < PHYS_MEM_PGS ; i++) {
			if ((InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page]
					& PTBL_MODIFIED_BIT) == 0) {
				victim = i;
				break;
			}
		}
	}
	if (victim == -1) {
		victim = 0;
	}
	return victim;
}

void selectDiskAndSector(INT32 victim, INT32 *disk, INT32 *sector) {
	*disk = (InvertedPageTable[victim]->pid + 1) % MAX_NUMBER_OF_DISKS;
	*sector = InvertedPageTable[victim]->page;
}
