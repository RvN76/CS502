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

extern UINT16 *Z502_PAGE_TBL_ADDR;
extern INT16 Z502_PAGE_TBL_LENGTH;

FrameAssignmentNode *InvertedPageTable[PHYS_MEM_PGS ];

UINT16 RoundsUnreferenced[PHYS_MEM_PGS ];

INT32 MostUnluckyVictim = 0;

SharedArea *SharedAreas = NULL;

INT32 memoryPrinter_DisplayGranularity = 0;

INT32 memoryPrinter_Count = 0;

//Allocate a frame for a page
void allocateFrame(INT32 page) {
	while (true) {
		INT32 i;
//		If there is a free frame, use it and record in the InvertedPageTable which pid and which one
//		of its page occupies it
		for (i = 0; i < PHYS_MEM_PGS ; i++) {
			if (!InvertedPageTable[i]) {
				FrameAssignmentNode *node = (FrameAssignmentNode *) calloc(1,
						sizeof(FrameAssignmentNode));
				node->pid = currentPCB->pid;
				node->pageTable = currentPCB->pageTable;
				node->page = page;
				InvertedPageTable[i] = node;
				currentPCB->pageTable[page] = i | PTBL_VALID_BIT;
				if (memoryPrinter_DisplayGranularity != 0) {
					memoryPrinter_Count++;
					if (memoryPrinter_Count % memoryPrinter_DisplayGranularity
							== 0) {
						memoryPrinter();
					}
				}
				return;
			}
		}

//		If there is no free frame at the moment, select a frame to be swapped out
		INT32 victim = chooseAndReset();
//		Notify the previous owner of this frame that the virtual page is now on the disk.
		InvertedPageTable[victim]->pageTable[InvertedPageTable[victim]->page] =
		PTBL_ON_DISK_BIT;
//		Reset the counter for this frame.
		RoundsUnreferenced[i] = 0;

//		Decide where to write the content of the frame.
		INT32 disk = (InvertedPageTable[victim]->pid
				+ InvertedPageTable[victim]->page / 16) % MAX_NUMBER_OF_DISKS
				+ 1;
		INT32 sector = InvertedPageTable[victim]->page;
		char *buffer = (char *) calloc(PGSIZE, sizeof(char));
		Z502ReadPhysicalMemory(victim, buffer);
		free(InvertedPageTable[victim]);
//		Label the frame as unoccupied.
		InvertedPageTable[victim] = NULL;
		requestForDisk(SYSNUM_DISK_WRITE, disk, sector, buffer);
//		Next time, this procedure starts over again until there is an unoccupied frame.
	}
}

void getThePageFromDisk(INT32 page) {
	char *buffer = (char *) calloc(PGSIZE, sizeof(char));
//	Find where the content of this page is on the disk
	INT32 disk = (currentPCB->pid + page / 16) % MAX_NUMBER_OF_DISKS + 1;
	INT32 sector = page;

//	Read the sector from the disk
	requestForDisk(SYSNUM_DISK_READ, disk, sector, buffer);
//	currentPCB->pageTable[page] = 0;
//	Allocate a frame for the content
	allocateFrame(page);
//	Overwrite the frame allocated with the content of the page
	Z502WritePhysicalMemory(currentPCB->pageTable[page] & PTBL_PHYS_PG_NO,
			buffer);
}

INT32 chooseAndReset() {
	INT32 victim = -1;
	INT32 i;

//	LRU with counter
	INT32 mostRounds = 0;
	for (i = 0; i < PHYS_MEM_PGS ; i++) {
//		If the frame has not been referenced after the last execution of clock algorithm,
//		increase its counter. Pick the frame with the highest counter, and clear those counter
//		of the referenced frames.
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
//	If there's no unreferenced frame, pick anyone.
	if (victim == -1) {
		for (i = 0; i < PHYS_MEM_PGS ; i++) {
			if ((InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page]
					& PTBL_REFERENCED_BIT) == 0) {
				victim = i;
				break;
			}
		}
	}

//	Basic LRU
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

////	FIFO
//	if (victim == -1) {
//		victim = MostUnluckyVictim;
//		MostUnluckyVictim = (MostUnluckyVictim + 1) % PHYS_MEM_PGS;
//	}
	return victim;
}

//Define a shared area
void defineSharedArea(INT32 startingAddress, INT32 numberOfPages, char *tag,
		INT32 *sharedID, INT32 *errCode) {
//	If the page table is not initialized yet, do it.
	if (!Z502_PAGE_TBL_ADDR) {
		currentPCB->pageTable = (UINT16 *) calloc(VIRTUAL_MEM_PAGES,
				sizeof(UINT16));
		currentPCB->pageTableLength = VIRTUAL_MEM_PAGES;
		Z502_PAGE_TBL_ADDR = currentPCB->pageTable;
		Z502_PAGE_TBL_LENGTH = VIRTUAL_MEM_PAGES;
	}

//	If an area with the same tag has been defined, map the given virtual address
//	with the previous physical address
	INT32 startingPage = startingAddress / PGSIZE;
	SharedArea *p = SharedAreas;
	while (p) {
		if (strcmp(p->tag, tag) == 0) {
			INT32 i;
			for (i = 0; i < numberOfPages; i++) {
				currentPCB->pageTable[startingPage + i] = (p->startingFrame + i)
						| PTBL_VALID_BIT;
			}
//			p->numberOfPreviousSharers++;
			*sharedID = ++p->numberOfPreviousSharers;
			*errCode = ERR_SUCCESS;
			return;
		}
		p = p->next;
	}

//	If this is a new tag, allocate some frames and record it in the SharedAreas list.
	SharedArea *newArea = (SharedArea *) calloc(1, sizeof(SharedArea));
	newArea->next = NULL;
	strcpy(newArea->tag, tag);
	newArea->numberOfPreviousSharers = 0;

	INT32 i;
	for (i = 0; i < numberOfPages; i++) {
		allocateFrame(startingPage + i);
	}

//	Record the starting physical address and length
	newArea->startingFrame = currentPCB->pageTable[startingPage]
			& PTBL_PHYS_PG_NO;
	newArea->length = numberOfPages;

	if (!SharedAreas) {
		SharedAreas = newArea;
	} else {
		SharedArea *q = SharedAreas;
		while (q->next) {
			q = q->next;
		}
		q->next = newArea;
	}

	*sharedID = 0;
	*errCode = ERR_SUCCESS;
}

//Print out every queue
void memoryPrinter() {
	int i;
	for (i = 0; i < PHYS_MEM_PGS ; i++) {
		if (InvertedPageTable[i]) {
			UINT16 entry =
					InvertedPageTable[i]->pageTable[InvertedPageTable[i]->page];
			MP_setup(i, InvertedPageTable[i]->pid, InvertedPageTable[i]->page,
					4 * ((entry & PTBL_VALID_BIT) != 0)
							+ 2 * ((entry & PTBL_MODIFIED_BIT) != 0)
							+ ((entry & PTBL_REFERENCED_BIT) != 0));
		}
	}

	MP_print_line();
}
