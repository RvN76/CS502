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

FrameAssignmentNode *InvertedPageTable[PHYS_MEM_PGS ];

UINT16 RoundsUnreferenced[PHYS_MEM_PGS ];

INT32 RoundDistribution[PHYS_MEM_PGS ];

INT32 MostUnluckyVictim = 0;

INT32 SwappedPageDistribution[VIRTUAL_MEM_PAGES];

void allocatePage(INT32 page) {
	while (true) {
		INT32 i;
		for (i = 0; i < PHYS_MEM_PGS ; i++) {
			if (!InvertedPageTable[i]) {
				FrameAssignmentNode *node = (FrameAssignmentNode *) calloc(1,
						sizeof(FrameAssignmentNode));
				node->pid = currentPCB->pid;
				node->pageTable = currentPCB->pageTable;
				node->page = page;
				InvertedPageTable[i] = node;
				char *new_buffer = (char*) calloc(PGSIZE, sizeof(char));
				Z502WritePhysicalMemory(i, new_buffer);
				currentPCB->pageTable[page] = i | PTBL_VALID_BIT;
				RoundsUnreferenced[i] = 0;
				return;
			}
		}
		INT32 victim = chooseAndReset();
		InvertedPageTable[victim]->pageTable[InvertedPageTable[victim]->page] =
		PTBL_MODIFIED_BIT;
		RoundsUnreferenced[i] = 0;
		SwappedPageDistribution[InvertedPageTable[victim]->page]++;
//		INT32 disk, sector;
//		sector = InvertedPageTable[victim]->page;
//		if ((InvertedPageTable[victim]->page) < 64) {
//			disk = ((InvertedPageTable[victim]->pid + 0) % MAX_NUMBER_OF_DISKS )
//					+ 1;
//		} else {
//			disk = ((InvertedPageTable[victim]->pid + 1) % MAX_NUMBER_OF_DISKS )
//					+ 1;
//		}

//		INT32 disk = (InvertedPageTable[victim]->pid + 1
//				+ InvertedPageTable[victim]->page / 16) % MAX_NUMBER_OF_DISKS;
//		INT32 sector = InvertedPageTable[victim]->page;
//		INT32 disk = (InvertedPageTable[victim]->pid+6) % MAX_NUMBER_OF_DISKS;
		INT32 disk = (InvertedPageTable[victim]->pid
				+ InvertedPageTable[victim]->page / 16) % MAX_NUMBER_OF_DISKS
				+ 1;
		INT32 sector = InvertedPageTable[victim]->page;
		char *buffer = (char*) calloc(PGSIZE, sizeof(char));
		Z502ReadPhysicalMemory(victim, buffer);
//		free(InvertedPageTable[victim]);
		InvertedPageTable[victim] = NULL;
//		char *new_buffer = (char*) calloc(PGSIZE, sizeof(char));
//		Z502WritePhysicalMemory(victim, new_buffer);
		requestForDisk(SYSNUM_DISK_WRITE, disk, sector, buffer);
	}
}

void getThePageFromDisk(INT32 page) {
	char *buffer = (char *) calloc(PGSIZE, sizeof(char));
//	char buffer[16];
//	INT32 disk, sector;
//	sector = page;
//	if (page < 64) {
//		disk = ((currentPCB->pid + 0) % MAX_NUMBER_OF_DISKS ) + 1;
//	} else {
//		disk = ((currentPCB->pid + 1) % MAX_NUMBER_OF_DISKS ) + 1;
//	}

//	INT32 disk = (currentPCB->pid + 1 + page / 128) % MAX_NUMBER_OF_DISKS;
//	INT32 sector = (page + 896) % VIRTUAL_MEM_PAGES;
	INT32 disk = (currentPCB->pid + page / 16) % MAX_NUMBER_OF_DISKS + 1;
	INT32 sector = page;
	requestForDisk(SYSNUM_DISK_READ, disk, sector, buffer);
//	currentPCB->pageTable[page] = 0;
	allocatePage(page);
	Z502WritePhysicalMemory(currentPCB->pageTable[page] & PTBL_PHYS_PG_NO,
			buffer);
}

INT32 chooseAndReset() {
	INT32 victim = -1;
	INT32 mostRounds = 0;
	INT32 i;
	for (i = 0; i < PHYS_MEM_PGS ; i++) {
		if ((InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page]
				& PTBL_REFERENCED_BIT) == 0) {
			RoundsUnreferenced[i]++;
			if (RoundsUnreferenced[i] > mostRounds) {
				mostRounds = RoundsUnreferenced[i];
				victim = i;
			}
		} else {
			RoundsUnreferenced[i] = 0;
			InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page] &=
					(PTBL_VALID_BIT | PTBL_MODIFIED_BIT | PTBL_PHYS_PG_NO);
		}
	}

	if (victim == -1) {
		for (i = 0; i < PHYS_MEM_PGS ; i++) {
			if ((InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page]
					& PTBL_REFERENCED_BIT) == 0) {
				victim = i;
				break;
			}
		}
	}

//	if (victim == -1) {
//		victim = MostUnluckyVictim;
//		MostUnluckyVictim = (MostUnluckyVictim + 1) % PHYS_MEM_PGS;
//	}
//	while (victim == -1) {
//		for (i = 0; i < PHYS_MEM_PGS ; i++) {
//			if ((InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page]
//					& PTBL_REFERENCED_BIT) == 0) {
//				if (victim == -1) {
//					victim = i;
//				}
//			} else {
//				InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page] &=
//						(PTBL_VALID_BIT | PTBL_MODIFIED_BIT | PTBL_PHYS_PG_NO);
//			}
//		}
//	}

//
//	RoundDistribution[RoundsUnreferenced[victim]]++;
	return victim;
}

void selectDiskAndSector(INT32 victim, INT32 *disk, INT32 *sector) {
	*disk = (InvertedPageTable[victim]->pid + 1) % MAX_NUMBER_OF_DISKS;
	*sector = InvertedPageTable[victim]->page;
}
