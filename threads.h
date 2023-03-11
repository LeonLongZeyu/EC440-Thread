#ifndef __THREADS__
#define __THREADS__

#include <stdio.h> 
#include <stdlib.h> 
#include <pthread.h>

//Function to return thread ID
pthread_t pthread_self(void);

//Function to create a thread
int pthread_create(
	pthread_t* thread,
    const pthread_attr_t* attr,
	void* (*start_routine) (void* ),
    void* arg);

//Function to exit a thread
void pthread_exit(void* value_ptr);

#endif