#ifndef __SERIAL_SERVICE_H
#define __SERIAL_SERVICE_H

#include <pthread.h>

#define BUFFER_SIZE 100
#define BAUDRATE 115200

typedef struct
{
	char buffer[BUFFER_SIZE];
	pthread_mutex_t mutex;
}sharedBuffer_t;


void* serial(void* param);


#endif