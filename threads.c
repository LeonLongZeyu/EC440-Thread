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

//bool
#define FALSE 0
#define TRUE  1

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
	jmp_buf regs;
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

void pthread_exit(void* value_ptr)
{
	bool REMAINING_THREADS = FALSE;

	//Change status to EXITED
	TCB_TABLE[(int)CURRENT_THREAD_ID].status = EXITED;

	//Mark the current thread as exited and set any waiting threads as ready
	pthread_t temp = TCB_TABLE[(int)CURRENT_THREAD_ID].TID;
	if (temp != CURRENT_THREAD_ID)
	{
		TCB_TABLE[(int)temp].status = READY;
	}

	// Check if there are any threads that are still running or ready to run
	for (int i = 0; i < MAX_NO_THREADS; i++)
	{
		if (TCB_TABLE[i].status != EXITED && TCB_TABLE[i].status != EMPTY)
		{
			REMAINING_THREADS = TRUE;
			break;
		}
	}

	//If there are any threads left, schedule another thread to run
	if (REMAINING_THREADS)
	{
		scheduler();
	}

	//Free the stack of any threads that have exited
	for (int j = 0; j < MAX_NO_THREADS; j++)
	{
		if (TCB_TABLE[j].status == EXITED)
		{
			free(TCB_TABLE[j].stack);
			TCB_TABLE[j].status = EMPTY;
		}
	}
}


int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine) (void* ), void* arg)
{
	attr = NULL; //As specified in the slides
	static int start;
	int MAIN_THREAD;

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
		TCB_TABLE[(int)temp].stack = malloc(STACK_SIZE);

    	if (TCB_TABLE[(int)temp].stack == NULL)
		{
			//Stack allocation failed
			//printf("ERROR: Failed to initialize stack.\n");
			exit(EXIT_FAILURE);
    	}

		//Setting up the context stack
		void* BOTTOM = TCB_TABLE[(int)temp].stack + STACK_SIZE;

		//Calculate the address where the address of pthread_exit() will be stored
		void* SP = BOTTOM - sizeof(void*);

		// Cast the function pointer to a void pointer and store it at the stack pointer
		void* EXIT_PTR = (void*) &pthread_exit;
		memcpy(SP, &EXIT_PTR, sizeof(EXIT_PTR));

		//Save thread regs state
		//setjmp(TCB_TABLE[(int)temp].regs);

		//Double pointer
		int* buf_array = (int*)TCB_TABLE[(int)temp].regs;

		//Change PC
        buf_array[JB_PC] = ptr_mangle((unsigned long int)start_thunk);

		//Change R12 to start_routine
		buf_array[JB_R12] = (unsigned long int) start_routine;

		//Change R13 to arg
		buf_array[JB_R12] = (long) arg;

		//Change SP
		buf_array[JB_RSP] = ptr_mangle((unsigned long int)SP);

		//Set thread ID
        TCB_TABLE[(int)temp].TID = temp;

		//Change status to READY
        TCB_TABLE[(int)temp].status = READY;

        scheduler();
	}
	else
	{
		MAIN_THREAD = 0;
	}

	return 0;
}
