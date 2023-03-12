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

//libc defintions
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC  7

//Max number of threads
#define MAX_NO_THREADS 128

//Periodic timer (ms)
#define TIMER 50000

//Thread stack size
#define STACK_SIZE 32767

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

//Signal handler for SIGALARM
struct sigaction SIGNAL_HANDLER;

/*struct sigaction
{
	void     (*sa_handler)(int);
	void     (*sa_sigaction)(int, siginfo_t *, void *);
	sigset_t   sa_mask;
	int        sa_flags;
	void     (*sa_restorer)(void);
};
*/

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
	jmp_buf regs[1];
	enum THREAD_STATUS status;
};

//Collection of threads in a table
struct TCB TCB_TABLE[MAX_NO_THREADS];


//pthread_self to return thread ID
pthread_t pthread_self(void)
{
	return CURRENT_THREAD_ID;
}

static void scheduler()
{
	pthread_t temp = CURRENT_THREAD_ID;
	int jump;

	//Set currently running thread to be ready as quantum has ended
	if (TCB_TABLE[(int)CURRENT_THREAD_ID].status == RUNNING)
	{
		TCB_TABLE[(int)CURRENT_THREAD_ID].status = READY;
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
		if (TCB_TABLE[(int)temp].status == READY)
		{
			break;
		}
	}

	//Save thread 'image' for thread that did not exit
	if (TCB_TABLE[(int)CURRENT_THREAD_ID].status != EXITED)
	{
		jump = setjmp(TCB_TABLE[(int)CURRENT_THREAD_ID].regs);
	}

	//Run found READY thread
	if (!jump)
	{
		TCB_TABLE[(int)temp].status = RUNNING;
		CURRENT_THREAD_ID = temp;
		longjmp(TCB_TABLE[(int)CURRENT_THREAD_ID].regs, 1);
	}
}

/*
int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine) (void* ), void* arg)
{
	attr = NULL; //As specified in the slides
	static int start;
	int MAIN_THREAD;
	int i = 1;

	//Initialize TCB and setup round robin with specified interval 50ms
	if (!start)
	{
		//Filling up TCB_TABLE
		for(int i = 0; i < MAX_NO_THREADS; i++)
		{
			TCB_TABLE[i].TID = i;
			TCB_TABLE[i].status = EMPTY;
		}

		if (!start)
		{
			//Setup the scheduler to SIGALRM with required interval
			useconds_t usecs = TIMER;
			useconds_t interval = TIMER;
			if (ualarm(usecs, interval) < 0)
			{
				//printf("ERROR: ualarm failed to initialize.\n");
			}

			//Round Robin
			if (sigemptyset(&SIGNAL_HANDLER.sa_mask) < 0) //sigset_t
			{
				//printf("ERROR: sigemptyset failed to initialize.\n");
			}
			else
			{
				SIGNAL_HANDLER.sa_handler = &scheduler; //Run the next thread after specified quantum
				SIGNAL_HANDLER.sa_flags = SA_NODEFER; //Do not prevent the signal from being received from within its own signal handler (source #12)
			}

			sigaction(SIGALRM, &SIGNAL_HANDLER, NULL);
		}

		//Change start
		start = 1;

		//Ready first thread
		TCB_TABLE[0].status = READY;

		//Main thread
		MAIN_THREAD = setjmp(TCB_TABLE[0].regs);
	}

	//Not main thread
	if (!MAIN_THREAD)
	{
		pthread_t temp = 1;
		
		while (TCB_TABLE[(int)temp].status != EMPTY && (int)temp < MAX_NO_THREADS)
		{
			temp++;
		}

		if ((int)temp >= MAX_NO_THREADS)
		{
			//printf("ERROR: MAX_NO_THREADS exceeded.\n");
			exit(EXIT_FAILURE);
		}

		*thread = temp;

		//Stack allocation
		TCB_TABLE[i].stack = malloc(STACK_SIZE);

    	if (TCB_TABLE[i].stack == NULL)
		{
			//Stack allocation failed
			//printf("ERROR: Failed to initialize stack.\n");
			exit(EXIT_FAILURE);
    	}

		//Save thread regs
		if (setjmp(TCB_TABLE[i].regs))
		{
			//printf("ERROR: Failed to save thread.\n");
			free(TCB_TABLE[i].stack);
			exit(EXIT_FAILURE);
    	}

		TCB_TABLE[i].regs[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int)start_thunk);
	}


	return 0;
}
*/
