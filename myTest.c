/*
 * myTest.c
 *
 *  Created on: 2014Äê10ÔÂ19ÈÕ
 *      Author: Dongyun
 */

#include "myTest.h"
#include "syscalls.h"
#include "protos.h"

#include         "stdio.h"
#include         "string.h"
#include         "stdlib.h"

extern long Z502_REG1;
extern long Z502_REG2;
extern long Z502_REG3;
extern long Z502_REG4;
extern long Z502_REG5;
extern long Z502_REG6;
extern long Z502_REG7;
extern long Z502_REG8;
extern long Z502_REG9;
extern INT16 Z502_MODE;

extern void SuccessExpected(INT32, char[]);

extern void ErrorExpected(INT32, char[]);

void myTest1m() {
	char message[64];
	INT32 sender;
	INT32 sendLength;

	GET_PROCESS_ID("", &Z502_REG2, &Z502_REG9);
	SuccessExpected(Z502_REG9, "GET_PROCESS_ID");

	CHANGE_PRIORITY(-1, MOST_FAVORABLE_PRIORITY, &Z502_REG9);
	SuccessExpected(Z502_REG9, "CHANGE_PRIORITY");

//	create a myTest1m_1
	CREATE_PROCESS("myTest1m_1", myTest1m_1, NORMAL_PRIORITY, &Z502_REG3,
			&Z502_REG9);
	SuccessExpected(Z502_REG9, "CREATE_PROCESS");

//	Let the myTest1m_1 terminate itself
	SLEEP(200);

//  receive from myTest1m_1. It should fail because we can't find
//	message from it and it's already dead
	RECEIVE_MESSAGE(Z502_REG3, message, 64, &sendLength, &sender, &Z502_REG9);
	ErrorExpected(Z502_REG9, "RECEIVE MESSAGE");

//	create a myTest1m_2
	CREATE_PROCESS("myTest1m_2", myTest1m_2, NORMAL_PRIORITY, &Z502_REG4,
			&Z502_REG9);
	SuccessExpected(Z502_REG9, "CREATE_PROCESS");

//	receive from myTest1m_2. It should succeed because when test1m recovers from suspension
//  (woken by the send of myTest1m_2), it will find that though myTest1m_2 is dead already,
//  but it does have existed and we can find the message from it in the message box
	RECEIVE_MESSAGE(Z502_REG4, message, 64, &sendLength, &sender, &Z502_REG9);
	SuccessExpected(Z502_REG9, "RECEIVE MESSAGE");

	printf("Receive from PID %d, message is: %s\n", (INT32) Z502_REG4, message);

	TERMINATE_PROCESS(-2, &Z502_REG9);
}

void myTest1m_1() {
	TERMINATE_PROCESS(-1, &Z502_REG9);
}

void myTest1m_2() {
	SEND_MESSAGE(0, "This is myTest1m_2", 32, &Z502_REG9);
	SuccessExpected(Z502_REG9, "CREATE_PROCESS");

	TERMINATE_PROCESS(-1, &Z502_REG9);
}

//This test shows that LRU (clock algorithm) actually works and
//can get better performance than FIFO in some cases. You can see the difference by
//enabling different part in chooseAndReset() in memoryManager.c (default enabled part is LRU,
//the commented part is FIFO).
void myTest2i() {
	GET_PROCESS_ID("", &Z502_REG2, &Z502_REG9);
	SuccessExpected(Z502_REG9, "GET_PROCESS_ID");

	CHANGE_PRIORITY(-1, MOST_FAVORABLE_PRIORITY, &Z502_REG9);
	SuccessExpected(Z502_REG9, "CHANGE_PRIORITY");

	INT32 i;

	INT32 data_to_write;
	INT32 data_read;

//	Allocate all the frames
	for (i = 0; i < PHYS_MEM_PGS ; i++) {
		data_to_write = i;
		MEM_WRITE(i*PGSIZE, &data_to_write);
	}

//	Replace one frame to start the clock algorithm(clear the PTBL_REFERENCE_BIT
//	for all owners of the frames), no matter FIFO or LRU is used, frame 0 will be swapped out
	data_to_write = 64;
	MEM_WRITE(64*PGSIZE, &data_to_write);

//	Reuse some previous pages and replace some others. Reuse of previous pages affects which pages to choose next using LRU,
//	but not FIFO. The inner loop will cause extra page replacement when using FIFO.
	int j;
	for (i = 1; i < PHYS_MEM_PGS / 2; i++) {
		for (j = 1; j < PHYS_MEM_PGS / 2; j++) {
			MEM_READ(j*PGSIZE, &data_read);
		}
		data_to_write = i + PHYS_MEM_PGS;
		MEM_WRITE((i + PHYS_MEM_PGS)*PGSIZE, &data_to_write);
	}

	for (i = 1; i < PHYS_MEM_PGS / 2; i++) {
		MEM_READ(i*PGSIZE, &data_read);
		MEM_READ((i + PHYS_MEM_PGS)*PGSIZE, &data_read);
	}

	TERMINATE_PROCESS(-2, &Z502_REG9);
}

//This tests shows that this system allows multiple shared areas with different tags(The total lengths of
//all shared areas cannot be larger than PHYS_MEM_PGS)

#define           NUMBER_MASTER_ITERATIONS    28
#define           PROC_INFO_STRUCT_TAG        1234
#define			  PROC_INFO_STRUCT_TAG1       2345
#define           SHARED_MEM_NAME             "almost_done!!\0"
#define			  SHARED_MEM_NAME1            "nearly_done!!\0"

#define           MOST_FAVORABLE_PRIORITY                       1
#define           NUMBER_2HX_PROCESSES                          5
#define           SLEEP_TIME_2H                             10000

// This is the per-process area for each of the processes participating
// in the shared memory.
typedef struct {
	INT32 structure_tag;      // Unique Tag so we know everything is right
	INT32 Pid;                // The PID of the slave owning this area
	INT32 TerminationRequest; // If non-zero, process should terminate.
	INT32 MailboxToMaster;    // Data sent from Slave to Master
	INT32 MailboxToSlave;     // Data sent from Master To Slave
	INT32 WriterOfMailbox;    // PID of process who last wrote in this area
} PROCESS_INFO;

// The following structure will be laid on shared memory by using
// the MEM_ADJUST   macro

typedef struct {
	PROCESS_INFO proc_info[NUMBER_2HX_PROCESSES + 1];
} SHARED_DATA;

// We use this local structure to gather together the information we
// have for this process.
typedef struct {
	INT32 StartingAddressOfSharedArea;     // Local Virtual Address
	INT32 PagesInSharedArea;               // Size of shared area
	// How OS knows to put all in same shared area
	char AreaTag[32];
	// Unique number supplied by OS. First must be 0 and the
	// return values must be monotonically increasing for each
	// process that's doing the Shared Memory request.
	INT32 OurSharedID;
	INT32 TargetShared;                   // Shared ID of slave we're sending to
	long TargetPid;                      // Pid of slave we're sending to
	INT32 ErrorReturned;

	long SourcePid;
	char ReceiveBuffer[20];
	long MessageReceiveLength;
	INT32 MessageSendLength;
	INT32 MessageSenderPid;
} LOCAL_DATA;

// This MEM_ADJUST macro allows us to overlay the SHARED_DATA structure
// onto the shared memory we've defined.  It generates an address
// appropriate for use by READ and MEM_WRITE.

#define         MEM_ADJUST( arg )                                       \
  (long)&(shared_ptr->arg) - (long)(shared_ptr)                             \
                      + (long)ld->StartingAddressOfSharedArea

#define         MEM_ADJUST2( shared, local, arg )                                       \
  (long)&(shared->arg) - (long)(shared)                             \
                      + (long)local->StartingAddressOfSharedArea

extern void test2hx();

extern void PrintTest2hMemory(SHARED_DATA *sp, LOCAL_DATA *ld);

void myTest2j() {
//	First half, same as test2h

	INT32 trash;

	GET_PROCESS_ID("", &Z502_REG4, &Z502_REG9);
	printf("\n\nRelease %s:Test 2h: Pid %ld\n", CURRENT_REL, Z502_REG4);
	CHANGE_PRIORITY(-1, MOST_FAVORABLE_PRIORITY, &Z502_REG5);

	CREATE_PROCESS("first", test2hx, 5, &trash, &Z502_REG5);
	CREATE_PROCESS("second", test2hx, 6, &trash, &Z502_REG5);
	CREATE_PROCESS("third", test2hx, 7, &trash, &Z502_REG5);
	CREATE_PROCESS("fourth", test2hx, 8, &trash, &Z502_REG5);
	CREATE_PROCESS("fifth", test2hx, 9, &trash, &Z502_REG5);

//	Second half, almost same as test2h, but the tag for shared ares is different

	CREATE_PROCESS("sixth", myTest2jx, 5, &trash, &Z502_REG5);
	CREATE_PROCESS("seventh", myTest2jx, 6, &trash, &Z502_REG5);
	CREATE_PROCESS("eighth", myTest2jx, 7, &trash, &Z502_REG5);
	CREATE_PROCESS("ninth", myTest2jx, 8, &trash, &Z502_REG5);
	CREATE_PROCESS("tenth", myTest2jx, 9, &trash, &Z502_REG5);

	// Loop here until the "2hx" final process terminate.

	Z502_REG9 = ERR_SUCCESS;
	while (Z502_REG9 == ERR_SUCCESS) {
		SLEEP(SLEEP_TIME_2H);
		GET_PROCESS_ID("first", &Z502_REG6, &Z502_REG9);
	}
	Z502_REG9 = ERR_SUCCESS;
	while (Z502_REG9 == ERR_SUCCESS) {
		SLEEP(SLEEP_TIME_2H);
		GET_PROCESS_ID("first", &Z502_REG6, &Z502_REG9);
	}
	TERMINATE_PROCESS(-2, &Z502_REG5);

}

void myTest2jx(){
	LOCAL_DATA *ld;
		SHARED_DATA *shared_ptr = 0;
		int Index;
		INT32 ReadWriteData;    // Used to move to and from shared memory

		ld = (LOCAL_DATA *) calloc(1, sizeof(LOCAL_DATA));
		if (ld == 0) {
			printf("Unable to allocate memory in test2hx\n");
		}
		strcpy(ld->AreaTag, SHARED_MEM_NAME1);

		GET_PROCESS_ID("", &Z502_REG4, &Z502_REG5);
		printf("\n\nRelease %s:Test 2hx: Pid %ld\n", CURRENT_REL, Z502_REG4);

		// As an interesting wrinkle, each process should start
		// its shared region at a somewhat different virtual address;
		// determine that here.
		ld->StartingAddressOfSharedArea = (Z502_REG4 % 17 + 20) * PGSIZE ;

		// This is the number of pages required in the shared area.
		ld->PagesInSharedArea = sizeof(SHARED_DATA) / PGSIZE + 1;

		// Now ask the OS to map us into the shared area
		DEFINE_SHARED_AREA((long )ld->StartingAddressOfSharedArea, // Input - our virtual address
				(long)ld->PagesInSharedArea,// Input - pages to map
				(long)ld->AreaTag,// Input - ID for this shared area
				&ld->OurSharedID,// Output - Unique shared ID
				&ld->ErrorReturned);              // Output - any error
		SuccessExpected(ld->ErrorReturned, "DEFINE_SHARED_AREA");

		ReadWriteData = PROC_INFO_STRUCT_TAG1; // Sanity data
		MEM_WRITE(MEM_ADJUST(proc_info[ld->OurSharedID].structure_tag),
				&ReadWriteData);

		ReadWriteData = Z502_REG4; // Store PID in our slot
		MEM_WRITE(MEM_ADJUST(proc_info[ld->OurSharedID].Pid), &ReadWriteData);
		ReadWriteData = 0;         // Initialize this counter
		MEM_WRITE(MEM_ADJUST(proc_info[ld->OurSharedID].MailboxToMaster),
				&ReadWriteData);
		ReadWriteData = 0;         // Initialize this counter
		MEM_WRITE(MEM_ADJUST(proc_info[ld->OurSharedID].MailboxToSlave),
				&ReadWriteData);

		//  This is the code used ONLY by the MASTER Process
		if (ld->OurSharedID == 0) {  //   We are the MASTER Process
			// Loop here the required number of times
			for (Index = 0; Index < NUMBER_MASTER_ITERATIONS; Index++) {

				// Wait for all slaves to start up - we assume after the sleep
				// that the slaves are no longer modifying their shared areas.
				SLEEP(1000); // Wait for slaves to start

				// Get slave ID we're going to work with - be careful here - the
				// code further on depends on THIS algorithm
				ld->TargetShared = (Index % ( NUMBER_2HX_PROCESSES - 1)) + 1;

				// Read the memory of that slave to make sure it's OK
				MEM_READ(MEM_ADJUST(proc_info[ld->TargetShared].structure_tag),
						&ReadWriteData);
				if (ReadWriteData != PROC_INFO_STRUCT_TAG1) {
					printf("We should see a structure tag, but did not\n");
					printf("This means that this memory is not mapped \n");
					printf("consistent with the memory used by the writer\n");
					printf("of this structure.  It's a page table problem.\n");
				}
				// Get the pid of the process we're working with
				MEM_READ(MEM_ADJUST(proc_info[ld->TargetShared].Pid),
						&ld->TargetPid);

				// We're sending data to the Slave
				MEM_WRITE(MEM_ADJUST(proc_info[ld->TargetShared].MailboxToSlave),
						&Index);
				MEM_WRITE(MEM_ADJUST(proc_info[ld->TargetShared].WriterOfMailbox),
						&Z502_REG4);
				ReadWriteData = 0;   // Do NOT terminate
				MEM_WRITE(
						MEM_ADJUST(proc_info[ld->TargetShared].TerminationRequest),
						&ReadWriteData);
				printf("Sender %ld to Receiver %d passing data %d\n", Z502_REG4,
						(int) ld->TargetPid, Index);

				// Check the iteration count of the slave.  If it tells us it has done a
				// certain number of iterations, then tell it to terminate itself.
				MEM_READ(MEM_ADJUST(proc_info[ld->TargetShared].MailboxToMaster),
						&ReadWriteData);
				if (ReadWriteData
						>= ( NUMBER_MASTER_ITERATIONS / (NUMBER_2HX_PROCESSES - 1))
								- 1) {
					ReadWriteData = 1;   // Do terminate
					MEM_WRITE(
							MEM_ADJUST(proc_info[ld->TargetShared].TerminationRequest),
							&ReadWriteData);
					printf("Master is sending termination message to PID %d\n",
							(int) ld->TargetPid);
				}

				// Now we are done with this slave - send it a message which will start it working.
				// The iterations may not be quite right - we may be sending a message to a
				// process that's already terminated, but that's OK
				SEND_MESSAGE(ld->TargetPid, " ", 1, &ld->ErrorReturned);
			}     // End of For Index
		}     // End of MASTER PROCESS

		// This is the start of the slave process work
		if (ld->OurSharedID != 0) {  //   We are a SLAVE Process
			// The slaves keep going forever until the master tells them to quit
			while ( TRUE ) {

				ld->SourcePid = -1; // From anyone
				ld->MessageReceiveLength = 20;
				RECEIVE_MESSAGE(ld->SourcePid, ld->ReceiveBuffer,
						ld->MessageReceiveLength, &ld->MessageSendLength,
						&ld->MessageSenderPid, &ld->ErrorReturned);
				SuccessExpected(ld->ErrorReturned, "RECEIVE_MESSAGE");

				// Make sure we have our memory mapped correctly
				MEM_READ(MEM_ADJUST(proc_info[ld->OurSharedID].structure_tag),
						&ReadWriteData);
				if (ReadWriteData != PROC_INFO_STRUCT_TAG1) {
					printf("We should see a structure tag, but did not.\n");
					printf("This means that this memory is not mapped \n");
					printf("consistent with the memory used when WE wrote\n");
					printf("this structure.  It's a page table problem.\n");
				}

				// Get the value placed in shared memory and compare it with the PID provided
				// by the messaging system.
				MEM_READ(MEM_ADJUST(proc_info[ld->OurSharedID].WriterOfMailbox),
						&ReadWriteData);
				if (ReadWriteData != ld->MessageSenderPid) {
					printf("ERROR: ERROR: The sender PID, given by the \n");
					printf("RECEIVE_MESSAGE and by the mailbox, don't match\n");
				}

				// We're receiving data from the Master
				MEM_READ(MEM_ADJUST(proc_info[ld->OurSharedID].MailboxToSlave),
						&ReadWriteData);
				MEM_READ(MEM_ADJUST(proc_info[ld->OurSharedID].WriterOfMailbox),
						&ld->MessageSenderPid);
				printf("Receiver %ld got message from %d passing data %d\n",
						Z502_REG4, ld->MessageSenderPid, ReadWriteData);

				// See if we've been asked to terminate
				MEM_READ(MEM_ADJUST(proc_info[ld->OurSharedID].TerminationRequest),
						&ReadWriteData);
				if (ReadWriteData > 0) {
					printf("Process %ld received termination message\n", Z502_REG4);
					TERMINATE_PROCESS(-1, &Z502_REG9);
				}

				// Increment the number of iterations we've done.  This will ultimately lead
				// to the master telling us to terminate.
				MEM_READ(MEM_ADJUST(proc_info[ld->OurSharedID].MailboxToMaster),
						&ReadWriteData);
				ReadWriteData++;
				MEM_WRITE(MEM_ADJUST(proc_info[ld->OurSharedID].MailboxToMaster),
						&ReadWriteData);

			}  //End of while TRUE
		}      // End of SLAVE

		// The Master comes here and prints out the entire shared area

		if (ld->OurSharedID == 0) {      // The slaves should terminate before this.
			SLEEP(5000);                        // Wait for msgs to finish
			printf("Overview of shared area at completion of Test2h\n");
			PrintTest2hMemory(shared_ptr, ld);
			TERMINATE_PROCESS(-1, &Z502_REG9);
		}              // END of if
		TERMINATE_PROCESS(-2, &Z502_REG9);
}
