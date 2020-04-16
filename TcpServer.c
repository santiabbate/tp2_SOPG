#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include "SerialService.h"
#include "TcpServer.h"


extern sharedBuffer_t toNet;    // Buffer y mutex para datos hacia InterfaceService
extern sharedBuffer_t toSerial; // Buffer y mutex para datos hacia la CIAA
extern int newDataToNet;
extern int newDataToSerial;

extern volatile sig_atomic_t exitFlag;  // Bandera para salir

// Funciones auxiliares
static void _copyBuffer(char* dst, char* src, pthread_mutex_t* mutex);
static int _haveToSend();
static void* _sendThread(void* param);
static void cleanup(void* param);

// Thread de inicialización del server y recepción de mensajes
void* tcpServer(void* param)
{
    pthread_t	tcpServerSendThread;        // Thread para manejo de datos salientes
    void* threadRetVal;

    int serverSocket, connectedSocket;
    struct sockaddr_in serverAddress, clientAddress;
    socklen_t  addrLen;
    addrLen = sizeof(clientAddress);
    
    char bufferIn[BUFFER_SIZE];

    // Creo el socket del servidor
	if ((serverSocket = socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		printf("Error abriendo socket\n");
        return 0;
	}

    // Inicializo la dirección para hacer el bind
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(PORT);
	serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1"); // Localhost

    // Bind a la dirección y puerto del servidor
	if (bind(serverSocket, (struct sockaddr*)&serverAddress,sizeof(serverAddress)) == -1)
	{
		printf("Error bind\n");
        return 0;
	}

    // Servidor escucha posibles clientes
	if (listen(serverSocket, 1) == -1)
	{
		printf("Error listen\n");
        return 0;
	}

    int retVal;  

    // Loop principal, acepta conexión al socket y recibe datos
    while (!exitFlag)
    {
        if ((connectedSocket = accept(serverSocket, (struct sockaddr*)&clientAddress,&addrLen)) <= 0)
		{
			printf("Error accept\n");
            continue;   // Intento nuevamente       
		}

        printf("Conexión desde %s:%d\n",inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));

        // En este punto la conexión está lista
        // Lanzo un thread para manejar el envío de mensajes (Datos salientes)
        if (0 != pthread_create(&tcpServerSendThread, NULL, _sendThread, (void*) (intptr_t) connectedSocket))
        {
            printf("Couldn't create send thread\n");
            close(connectedSocket);
            return 0;
        }

        // Como cancelo el thread server desde el main thread, 
        // quise hacer la prueba de utilizar las funciones de cleanup del thread.
        // Registro en este punto la función cleanup, para que cuando el thread tcpServer
        // sea cancelado, ejecute cleanup() y dentro de esa función se cancela el thread _sendThread
        pthread_cleanup_push(cleanup, (void*) tcpServerSendThread);

        // Loop de recepción de mensajes
        do
        {   
            retVal  = recv(connectedSocket, bufferIn, BUFFER_SIZE, 0);
            if (retVal > 0)
            {
                _copyBuffer(toSerial.buffer, bufferIn, &toSerial.mutex);   // Thread safe copy
                printf("New data to Serial: %s\n",toSerial.buffer);

                pthread_mutex_lock(&toSerial.mutex);
                    // Levanto bandera de nuevo data hacia la CIAA
                    newDataToSerial = 1;
                pthread_mutex_unlock(&toSerial.mutex);
            }
        } while ((retVal > 0) && (!exitFlag));       // Se cerró la conexión
        
        // Si salí por EOF cancelo el _sendThread ya que se cerró la conexión
        pthread_cleanup_pop(tcpServerSendThread);
        close(connectedSocket);
        printf("Conexión cerrada, espero nuevo cliente...\n");
    }
    
    return 0;
}

// Thread para manejo de datos salientes
void* _sendThread(void* param)
{
    int connectedSocket = (intptr_t) param;
    char bufferOut[BUFFER_SIZE];
    
    // Loop de escritura de datos al socket
    while (1)
    {
        usleep(10000);
        if (_haveToSend())  // Thread safe check
        {            
            _copyBuffer(bufferOut, toNet.buffer, &toNet.mutex); // Thread safe copy

            send(connectedSocket, bufferOut, strlen(bufferOut), 0);
        }
    }

    return 0;
}

int _haveToSend()
{
    int flag;
    pthread_mutex_lock(&toNet.mutex);
        flag = newDataToNet;
        newDataToNet = 0;
    pthread_mutex_unlock(&toNet.mutex);
    return flag;
}

static void _copyBuffer(char* dst, char* src, pthread_mutex_t* mutex)
{
    pthread_mutex_lock(mutex);
        strcpy(dst, src);
    pthread_mutex_unlock(mutex);
}

static void cleanup(void* param)
{
    pthread_t thread = (pthread_t) param;
    void* rv;
    
    if (0 != pthread_cancel(thread))
	{
		printf("No pudo cancelar send thread\n");
	}    

	if (0 != pthread_join(thread, &rv))
	{
		printf("Couldn't join\n");
	}
    if (rv == PTHREAD_CANCELED)
    {
        printf("Cancelación send thread OK\n");
    }

}