/*
 * memoryManager.c
 *
 *  Created on: 2014Äê11ÔÂ4ÈÕ
 *      Author: Dongyun
 */

#include "memoryManager.h"
#include "mySVC.h"
#include "string.h"
#include "stdlib.h"

FrameAssignmentNode *addressList = NULL;

extern UINT16 *Z502_PAGE_TBL_ADDR;

void initializeSlot(INT32 slot) {
	FrameAssignmentNode *node = (FrameAssignmentNode *) calloc(1, sizeof(FrameAssignmentNode));
	node->pid = currentPCB->pid;
	node->next = NULL;
	if (!addressList) {
		node->frame = 0;
		addressList = node;
	} else {
		FrameAssignmentNode *p = addressList;
		if (!p->next) {
			node->frame = p->frame + 1;
			p->next = node;
		} else {
			while (p->next) {
				if (p->next->frame - p->frame > 1) {
					node->frame = p->frame + 1;
					node->next = p->next;
					p->next = node;
					break;
				} else {
					p = p->next;
				}
			}
			if (!p->next) {
				node->frame = p->frame + 1;
				p->next = node;
			}
		}
	}
	Z502_PAGE_TBL_ADDR[slot] = node->frame | PTBL_VALID_BIT;
//	Z502_PAGE_TBL_ADDR[slot] = pageTable[slot] | PTBL_VALID_BIT;
}

