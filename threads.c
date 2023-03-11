#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>

#include "threads.h"

//Max number of threads
#define MAX_NO_THREADS 128

//Periodic timer (ms)
#define TIMER 50000

//Thread stack size
#define STACK_SIZE 32767

//libc defintions
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC  7

//Enumeration
enum THREAD_STATUS
{
	READY,
	RUNNING,
	EXITED,
	BLOCKED,
	EMPTY	
};

//Running thread ID
pthread_t CURRENT_THREAD_ID = 0;

//Collection of threads in a table
struct TCB TCB_TABLE[MAX_NO_THREADS];

//Signal handler for SIGALARM
struct sigaction SIGNAL_HANDLER;

//Mangle function
static unsigned long int ptr_mangle(unsigned long int p)
{
    unsigned long int ret;

    asm("movq %1, %%rax;\n"
        "xorq %%fs:0x30, %%rax;"
        "rolq $0x11, %%rax;"
        "movq %%rax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%rax"
    );

    return ret;
}

//Start routine
static void *start_thunk()
{
  asm("popq %%rbp;\n"
      "movq %%r13, %%rdi;\n"
      "pushq %%r12;\n"
      "retq;\n"
      :
      :
      : "%rdi"
  );

  __builtin_unreachable();
}

//Demangle function
static unsigned long int ptr_demangle(unsigned long int p)
{
    unsigned long int ret;
    asm("movq %1, %%rax;\n"
        "rorq $0x11, %%rax;"
        "xorq %%fs:0x30, %%rax;"
        "movq %%rax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%rax"
    );
    return ret;
}

//Thread control block
struct TCB
{
	pthread_t TID;
	void* stack;
	jmp_buf regs;
	enum THREAD_STATUS status;
};


//pthread_self to return thread ID
pthread_t pthread_self(void)
{
	return CURRENT_THREAD_ID;
}

static void scheduler()
{
	pthread_t temp = CURRENT_THREAD_ID;
	int jump;

	if (TCB_TABLE[CURRENT_THREAD_ID].status == RUNNING)
	{
		TCB_TABLE[CURRENT_THREAD_ID].status = READY;
	}

	//Iterating through TCB_TABLE to look for READY thread
	while (1)
	{
		//Loops through TCB_TABLE in a circle
		if (temp == MAX_NO_THREADS - 1)
		{
			temp = 0;
		}
		else
		{
			temp++;
		}

		//Break once a READY thread is found
		if (TCB_TABLE[temp].status == READY)
		{
			break;
		}
	}

	//Save thread 'image' for thread that did not exit
	if (TCB_TABLE[CURRENT_THREAD_ID].status != EXITED)
	{
		jump = setjmp(TCB_TABLE[CURRENT_THREAD_ID].regs);
	}

	//Run found READY thread
	if (!jump)
	{
		TCB_TABLE[temp].status = RUNNING;
		CURRENT_THREAD_ID = temp;
		longjmp(TCB_TABLE[CURRENT_THREAD_ID].regs, 1);
	}
}

/*
int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine) (void* ), void* arg)
{
	static int start;
	int MAIN_THREAD;

	//Initialize TCB and setup round robin with specified interval 50ms
	if (!start)
	{
		//Filling up TCB_TABLE
		for(int i = 0; i < MAX_THREADS; i++)
		{
			TCB_TABLE[i].TID = i;
			TCB_TABLE[i].status = EMPTY;
		}

		// Setup the scheduler to SIGALRM with required interval
		__useconds_t usecs = TIMER;
		__useconds_t interval = TIMER;
		ualarm(usecs, interval);

		// Round Robin
		sigemptyset(&SIGNAL_HANDLER.sa_mask);
		SIGNAL_HANDLER.sa_handler = &schedule;
		SIGNAL_HANDLER.sa_flags = SA_NODEFER;
		sigaction(SIGALRM, &SIGNAL_HANDLER, NULL);

		//Change start
		start = 1;

		//Ready first thread
		TCB_TABLE[0].status = READY;

		//Main thread
		MAIN_THREAD = setjmp(TCB_TABLE[0].regs);
	}

	if (!MAIN_THREAD)
	{
		pthread_t temp = 1;
		
		while (TCB_TABLE[temp].status != EMPTY && temp < MAX_NO_THREADS)
	}

	return 0;
}
*/