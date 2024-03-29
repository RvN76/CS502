/************************************************************************

 This code forms the base of the operating system you will
 build.  It has only the barest rudiments of what you will
 eventually construct; yet it contains the interfaces that
 allow test.c and z502.c to be successfully built together.

 Revision History:
 1.0 August 1990
 1.1 December 1990: Portability attempted.
 1.3 July     1992: More Portability enhancements.
 Add call to sample_code.
 1.4 December 1992: Limit (temporarily) printout in
 interrupt handler.  More portability.
 2.0 January  2000: A number of small changes.
 2.1 May      2001: Bug fixes and clear STAT_VECTOR
 2.2 July     2002: Make code appropriate for undergrads.
 Default program start is in test0.
 3.0 August   2004: Modified to support memory mapped IO
 3.1 August   2004: hardware interrupt runs on separate thread
 3.11 August  2004: Support for OS level locking
 4.0  July    2013: Major portions rewritten to support multiple threads
 ************************************************************************/

#include       		"global.h"
#include            "syscalls.h"
#include			"mySVC.h"
#include			"myInterrupts.h"
#include			"memoryManager.h"
#include			"myTest.h"
#include            "protos.h"
#include            "string.h"

// These locations are global and define information about the page table
extern UINT16 *Z502_PAGE_TBL_ADDR;
extern INT16 Z502_PAGE_TBL_LENGTH;

extern void *TO_VECTOR[];

char *call_names[] = { "mem_read ", "mem_write", "read_mod ", "get_time ",
		"sleep    ", "get_pid  ", "create   ", "term_proc", "suspend  ",
		"resume   ", "ch_prior ", "send     ", "receive  ", "disk_read",
		"disk_wrt ", "def_sh_ar" };

/************************************************************************
 INTERRUPT_HANDLER
 When the Z502 gets a hardware interrupt, it transfers control to
 this routine in the OS.
 ************************************************************************/
void interrupt_handler(void) {
	INT32 device_id;
	INT32 status;
	INT32 Index = 0;
	static BOOL remove_this_in_your_code = TRUE; /** TEMP **/
	static INT32 how_many_interrupt_entries = 0; /** TEMP **/

	// Get cause of interrupt
	MEM_READ(Z502InterruptDevice, &device_id);
	// Set this device as target of our query
	MEM_WRITE(Z502InterruptDevice, &device_id);
	// Now read the status of this device
	MEM_READ(Z502InterruptStatus, &status);

	/** REMOVE THE NEXT SIX LINES **/
	how_many_interrupt_entries++; /** TEMP **/
	if (interrupt_DisplayGranularity != 0) {
		interrupt_Count++;
		if (interrupt_Count % interrupt_DisplayGranularity == 0) {
			if (remove_this_in_your_code && (how_many_interrupt_entries < 20)) {
				printf("Interrupt_handler: Found device ID %d with status %d\n",
						device_id, status);
			}
		}
	}

	//Call the timerInterrupt() routine
	switch (device_id) {
	case TIMER_INTERRUPT :
		timerInterrupt();
		break;
	case DISK_INTERRUPT_DISK1 :
	case DISK_INTERRUPT_DISK2 :
	case DISK_INTERRUPT_DISK1 + 2:
	case DISK_INTERRUPT_DISK1 + 3:
	case DISK_INTERRUPT_DISK1 + 4:
	case DISK_INTERRUPT_DISK1 + 5:
	case DISK_INTERRUPT_DISK1 + 6:
	case DISK_INTERRUPT_DISK1 + 7:
		diskInterrupt(device_id - 4);
		break;
	default:
		break;
	}
	// Clear out this device - we're done with it
	MEM_WRITE(Z502InterruptClear, &Index);
} /* End of interrupt_handler */
/************************************************************************
 FAULT_HANDLER
 The beginning of the OS502.  Used to receive hardware faults.
 ************************************************************************/

void fault_handler(void) {
	INT32 device_id;
	INT32 status;
	INT32 Index = 0;

	// Get cause of interrupt
	MEM_READ(Z502InterruptDevice, &device_id);
	// Set this device as target of our query
	MEM_WRITE(Z502InterruptDevice, &device_id);
	// Now read the status of this device
	MEM_READ(Z502InterruptStatus, &status);

	if (fault_DisplayGranularity != 0) {
		fault_Count++;
		if (fault_Count % fault_DisplayGranularity == 0) {
			printf("Fault_handler: Found vector type %d with value %d\n",
					device_id, status);
		}
	}

	// Clear out this device - we're done with it
	MEM_WRITE(Z502InterruptClear, &Index);

	switch (device_id) {
	case PRIVILEGED_INSTRUCTION :
//	Terminate the current process
		printf("Terminate current process\n");
		INT32 termResult;
		terminateProcess(-1, &termResult);
		break;
	case INVALID_MEMORY :
//		if (!InvertedPageTable) {
//			InvertedPageTable = (FrameAssignmentNode **) calloc(
//			PHYS_MEM_PGS, sizeof(FrameAssignmentNode *));
//			RoundsUnreferenced = (UINT16 *) calloc(PHYS_MEM_PGS,
//					sizeof(UINT16));
//		}
		if (!Z502_PAGE_TBL_ADDR) {
			currentPCB->pageTable = (UINT16 *) calloc(VIRTUAL_MEM_PAGES,
					sizeof(UINT16));
			currentPCB->pageTableLength = VIRTUAL_MEM_PAGES;
			Z502_PAGE_TBL_ADDR = currentPCB->pageTable;
			Z502_PAGE_TBL_LENGTH = VIRTUAL_MEM_PAGES;
		}
		if (status < Z502_PAGE_TBL_LENGTH && status >= 0) {
			if ((currentPCB->pageTable[status] & PTBL_ON_DISK_BIT) == 0) {
				allocateFrame(status);
			} else {
				getThePageFromDisk(status);
			}
		} else {
			printf("Invalid page number: %d\n", status);
			printf("Terminate current process\n");
			INT32 termResult;
			terminateProcess(-1, &termResult);
		}
		break;
	}

} /* End of fault_handler */

/************************************************************************
 SVC
 The beginning of the OS502.  Used to receive software interrupts.
 All system calls come to this point in the code and are to be
 handled by the student written code here.
 The variable do_print is designed to print out the data for the
 incoming calls, but does so only for the first ten calls.  This
 allows the user to see what's happening, but doesn't overwhelm
 with the amount of data.
 ************************************************************************/

typedef union {
	char char_data[PGSIZE ];
	UINT32 int_data[PGSIZE / sizeof(int)];
} DISK_DATA;

void svc(SYSTEM_CALL_DATA *SystemCallData) {
	short call_type;
	static short do_print = 10;
	short i;
	INT32 Time;

	call_type = (short) SystemCallData->SystemCallNumber;
	if (do_print > 0) {
		printf("SVC handler: %s\n", call_names[call_type]);
		for (i = 0; i < SystemCallData->NumberOfArguments - 1; i++) {
			//Value = (long)*SystemCallData->Argument[i];
			printf("Arg %d: Contents = (Decimal) %8ld,  (Hex) %8lX\n", i,
					(unsigned long) SystemCallData->Argument[i],
					(unsigned long) SystemCallData->Argument[i]);
		}
		do_print--;
	}
	switch (call_type) {
//  Get time service call
	case SYSNUM_GET_TIME_OF_DAY:	//The value is found in syscalls.h
		CALL(MEM_READ(Z502ClockStatus, &Time))
		;
		*(INT32 *) SystemCallData->Argument[0] = Time;
		break;
//	Call for a sleep
	case SYSNUM_SLEEP:
		sleepProcess((INT32) SystemCallData->Argument[0]);
//		start_Timer((INT32) SystemCallData->Argument[0]);
		break;
//	Call to create a process
	case SYSNUM_CREATE_PROCESS:
		createProcess((char *) SystemCallData->Argument[0],
				(void *) SystemCallData->Argument[1],
				(INT32) SystemCallData->Argument[2],
				(INT32 *) SystemCallData->Argument[3],
				(INT32 *) SystemCallData->Argument[4]);
		break;
//	Call to terminate a process
	case SYSNUM_TERMINATE_PROCESS:
		terminateProcess((INT32) SystemCallData->Argument[0],
				(INT32 *) SystemCallData->Argument[1]);
		break;
//	Call to suspend a process
	case SYSNUM_SUSPEND_PROCESS:
		suspendProcess((INT32) SystemCallData->Argument[0],
				(INT32 *) SystemCallData->Argument[1]);
		break;
//	Call to resume a process
	case SYSNUM_RESUME_PROCESS:
		resumeProcess((INT32) SystemCallData->Argument[0],
				(INT32 *) SystemCallData->Argument[1]);
		break;
//	Call to get a process id
	case SYSNUM_GET_PROCESS_ID:
		getProcessID((char *) SystemCallData->Argument[0],
				(INT32 *) SystemCallData->Argument[1],
				(INT32 *) SystemCallData->Argument[2]);
		break;
//	Call to change the priority
	case SYSNUM_CHANGE_PRIORITY:
		changePriority((INT32) SystemCallData->Argument[0],
				(INT32) SystemCallData->Argument[1],
				(INT32 *) SystemCallData->Argument[2]);
		break;
//	Call to send a message
	case SYSNUM_SEND_MESSAGE:
		sendMessage((INT32) SystemCallData->Argument[0],
				(char *) SystemCallData->Argument[1],
				(INT32) SystemCallData->Argument[2],
				(INT32 *) SystemCallData->Argument[3]);
		break;
//	Call to receive a message
	case SYSNUM_RECEIVE_MESSAGE:
		receiveMessage((INT32) SystemCallData->Argument[0],
				(char *) SystemCallData->Argument[1],
				(INT32) SystemCallData->Argument[2],
				(INT32 *) SystemCallData->Argument[3],
				(INT32 *) SystemCallData->Argument[4],
				(INT32 *) SystemCallData->Argument[5]);
		break;
//	Call to read from disk
	case SYSNUM_DISK_READ:
//	Call to write to disk
	case SYSNUM_DISK_WRITE:
		requestForDisk(call_type, (INT32) SystemCallData->Argument[0],
				(INT32) SystemCallData->Argument[1],
				(char *) SystemCallData->Argument[2]);
//	}
		break;
//	call to define a shared area
	case SYSNUM_DEFINE_SHARED_AREA:
		defineSharedArea((INT32) SystemCallData->Argument[0],
				(INT32) SystemCallData->Argument[1],
				(char *) SystemCallData->Argument[2],
				(INT32 *) SystemCallData->Argument[3],
				(INT32 *) SystemCallData->Argument[4]);
		break;
//	other cases: report
	default:
		printf("ERROR! Can't recognize call_type\n");
		printf("call_type is : %i", call_type);
		break;
	}
}                                               // End of svc

/************************************************************************
 osInit
 This is the first routine called after the simulation begins.  This
 is equivalent to boot code.  All the initial OS components can be
 defined and initialized here.
 ************************************************************************/

void osInit(int argc, char *argv[]) {
	void *next_context;
	INT32 i;
	//TimerQueue = NULL;

	/* Demonstrates how calling arguments are passed thru to here       */

	printf("Program called with %d arguments:", argc);
	for (i = 0; i < argc; i++)
		printf(" %s", argv[i]);
	printf("\n");
	printf("Calling with argument 'sample' executes the sample program.\n");

	/*          Setup so handlers will come to code in base.c           */

	TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR ] = (void *) interrupt_handler;
	TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR ] = (void *) fault_handler;
	TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR ] = (void *) svc;

	/*  Determine if the switch was set, and if so go to demo routine.  */

	if ((argc > 1) && (strcmp(argv[1], "sample") == 0)) {
		Z502MakeContext(&next_context, (void *) sample_code,
		KERNEL_MODE);
		Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &next_context);
	} /* This routine should never return!!           */

	/*  This should be done by a "os_make_process" routine, so that
	 test0 runs on a process recognized by the operating system.    */

	void *testToRun = NULL;

	if (argc > 1) {
		if (strcmp(argv[1], "test0") == 0) {
			testToRun = (void *) test0;
		}

		if (strcmp(argv[1], "test1a") == 0) {
			testToRun = (void *) test1a;
		}

		if (strcmp(argv[1], "test1b") == 0) {
			testToRun = (void *) test1b;
		}

		if (strcmp(argv[1], "test1c") == 0) {
			testToRun = (void *) test1c;
		}

		if (strcmp(argv[1], "test1d") == 0) {
			testToRun = (void *) test1d;
		}

		if (strcmp(argv[1], "test1e") == 0) {
			testToRun = (void *) test1e;
		}

		if (strcmp(argv[1], "test1f") == 0) {
			testToRun = (void *) test1f;
		}

		if (strcmp(argv[1], "test1g") == 0) {
			testToRun = (void *) test1g;
		}

		if (strcmp(argv[1], "test1h") == 0) {
			testToRun = (void *) test1h;
		}

		if (strcmp(argv[1], "test1i") == 0) {
			testToRun = (void *) test1i;
		}

		if (strcmp(argv[1], "test1j") == 0) {
			testToRun = (void *) test1j;
		}

		if (strcmp(argv[1], "test1k") == 0) {
			testToRun = (void *) test1k;
		}

		if (strcmp(argv[1], "test1l") == 0) {
			testToRun = (void *) test1l;
		}

		if (strcmp(argv[1], "test1m") == 0) {
			testToRun = (void *) myTest1m;
		}

		if (strcmp(argv[1], "test2a") == 0) {
			testToRun = (void *) test2a;
			memoryPrinter_DisplayGranularity = 1;
			fault_DisplayGranularity = 1;
			interrupt_DisplayGranularity = 1;
		}

		if (strcmp(argv[1], "test2b") == 0) {
			testToRun = (void *) test2b;
			memoryPrinter_DisplayGranularity = 1;
			fault_DisplayGranularity = 1;
			interrupt_DisplayGranularity = 1;
		}

		if (strcmp(argv[1], "test2c") == 0) {
			testToRun = (void *) test2c;
			schedulerPrinter_DisplayGranularity = 1;
			fault_DisplayGranularity = 10;
			interrupt_DisplayGranularity = 10;
		}

		if (strcmp(argv[1], "test2d") == 0) {
			testToRun = (void *) test2d;
			schedulerPrinter_DisplayGranularity = 10;
			fault_DisplayGranularity = 10;
			interrupt_DisplayGranularity = 10;
		}

		if (strcmp(argv[1], "test2e") == 0) {
			testToRun = (void *) test2e;
			schedulerPrinter_DisplayGranularity = 10;
			memoryPrinter_DisplayGranularity = 100;
			fault_DisplayGranularity = 100;
			interrupt_DisplayGranularity = 10;
		}

		if (strcmp(argv[1], "test2f") == 0) {
			testToRun = (void *) test2f;
			memoryPrinter_DisplayGranularity = 200;
			fault_DisplayGranularity = 100;
			interrupt_DisplayGranularity = 100;
		}

		if (strcmp(argv[1], "test2g") == 0) {
			testToRun = (void *) test2g;
		}

		if (strcmp(argv[1], "test2h") == 0) {
			testToRun = (void *) test2h;
		}

		if (strcmp(argv[1], "test2i") == 0) {
			testToRun = (void *) myTest2i;
		}

		if (strcmp(argv[1], "test2j") == 0) {
			testToRun = (void *) myTest2j;
		}
	}

	if (testToRun) {
		osCreateProcess(testToRun);
	}

//    Z502MakeContext( &next_context, (void *)test1a, USER_MODE );
//    Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &next_context );
}                                               // End of osInit
