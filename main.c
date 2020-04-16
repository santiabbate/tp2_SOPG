#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include "SerialService.h"
#include "SerialManager.h"
#include "TcpServer.h"

sharedBuffer_t toNet;		// Buffer y mutex para datos hacia InterfaceService
sharedBuffer_t toSerial;	// Buffer y mutex para datos hacia la CIAA
int newDataToNet;
int newDataToSerial;

void bloquearSign(void);
void desbloquearSign(void);

volatile sig_atomic_t exitFlag = 0;	// Bandera para salir

void exitHandler(int sig)
{
    exitFlag = 1;
}

int main(int argc, char const *argv[])
{
	printf("Inicio Serial Service\r\n");

	if (argc < 2)
	{
		printf("ERROR: Ingresar número del puerto serie\n");
		exit(EXIT_FAILURE);
	}
	
	// Inicializo puerto serie
	int serialNumber = atoi(argv[1]);
	if (serial_open(serialNumber, BAUDRATE))
    {
        printf("Error conectando a la CIAA\n");
		exit(EXIT_FAILURE);
    }

	// Inicializo señales SIGINT y SIGTERM
	struct sigaction sigint;
    sigint.sa_handler = exitHandler;
	sigint.sa_flags = 0;
    sigemptyset(&sigint.sa_mask);
	if ((sigaction(SIGINT,&sigint,NULL)) == -1)
		exit(EXIT_FAILURE);

	struct sigaction sigterm;
    sigterm.sa_handler = exitHandler;
	sigterm.sa_flags = 0;
    sigemptyset(&sigterm.sa_mask);
	if ((sigaction(SIGTERM,&sigterm,NULL)) == -1)
		exit(EXIT_FAILURE);

	// Utilizo la función propuesta, para bloquaer las señales de todos los threads antes de crear un thread nuevo
    bloquearSign();

	pthread_t	tcpServerThread;
	void* threadsRetVal;
	pthread_mutex_init(&toNet.mutex,NULL);
	pthread_mutex_init(&toSerial.mutex,NULL);

	// Init TCP Server thread
	if (0 != pthread_create(&tcpServerThread, NULL, tcpServer, (void*) 0))
	{
		printf("Couldn't create thread server\n");
		exit(EXIT_FAILURE);
	}

	// Desbloqueo señales, el thread main es el que se encarga de procesar las señales
	desbloquearSign();
	
	// Loop principal, maneja transmisión y recepción de datos por el puerto serie
	while (!exitFlag)
	{
        usleep(10000);

        pthread_mutex_lock(&toSerial.mutex);
			// Envío datos al puerto serie, si desde el thread server TCP se informa que hay datos nuevos
			// Ejecuto atómicamente porque el thread server accede a la estructura toSerial
			if (newDataToSerial)
            {
                serial_send(toSerial.buffer,strlen(toSerial.buffer));
                newDataToSerial = 0;
            }
    	pthread_mutex_unlock(&toSerial.mutex);

        pthread_mutex_lock(&toNet.mutex);
			// Hago un polling del puerto serie para recibir datos de la CIAA.
			// Si hubo datos nuevos para ser retransmitidos, levanto la bandera para avisar al thread server TCP
			// Ejecuto atómicamente porque el thread server accede a la estructura toNet
			if (serial_receive(toNet.buffer, BUFFER_SIZE))
			// serial_receive(toNet.buffer, BUFFER_SIZE);
			{
				printf("New data to Net: %s\n", toNet.buffer);
				newDataToNet = 1;
			}
    	pthread_mutex_unlock(&toNet.mutex);
        
	}
	
	// Saliendo del loop principal, cancelo el thread del server TCP
	printf("Exit signal\n");
	if (0 != pthread_cancel(tcpServerThread))
	{
		printf("Couldn't cancel\n");
	}

	if (0 != pthread_join(tcpServerThread, &threadsRetVal))
	{
		printf("Couldn't join\n");
	}

	if (threadsRetVal == PTHREAD_CANCELED)
	{
		printf("Cancelación server thread OK. Saliendo..\n");
	}

    serial_close();

	exit(EXIT_SUCCESS);
}


void bloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void desbloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}