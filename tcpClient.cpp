#include <stdio.h>
#include <winsock2.h>
#include <locale.h>

#pragma comment(lib,"ws2_32.lib")

#define SEND_BUFFER_LENGTH 1024
#define RECV_BUFFER_LENGTH 1024

static WSADATA wsa;
static SOCKET commandSocket;

static char sendBuffer[SEND_BUFFER_LENGTH];
static char recvBuffer[RECV_BUFFER_LENGTH];

static SOCKET socketCreate();
static int socketConnect(SOCKET s, char *addr, int port);

static int ftpSendCommand(char *cmd);
static int ftpRecieveRespond(char *responseBuffer, int sizeOfResponseBufferint);
static int ftpEnterPasv(char *ipaddr, int *port);
static int ftpLogin(char *addr, int port, char *username, char *password);
static void ftpQuit();
static int ftpInit();

static int ftpSetUpDataSocket(SOCKET *s, char *ipAddress, int port);
static LPVOID createDataStreamThread(char *pasvIpAddress, int pasvPort, HANDLE *hDataStreamThread);
DWORD WINAPI ftpHandleDataStream(LPVOID lpParam);

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "Russian");

	char ipAdressStr[16], loginStr[64], passwordStr[256];
	int port;
	printf("Enter server IP adress: ");
	scanf("%s", ipAdressStr);
	printf("Enter port number: ");
	scanf("%d", &port);
	printf("Enter login: ");
	scanf("%s", loginStr);
	printf("Enter password: ");
	scanf("%s", passwordStr);

	ftpInit();
	if (ftpLogin(ipAdressStr, port, loginStr, passwordStr) != 0)
	{
		printf("Login error!\n");
		ftpQuit();
		return 0;
	}

	char cmnd[64];
	char responseBuffer[1024];
	SecureZeroMemory(responseBuffer, 1024);
	while (true)
	{
		fgets(cmnd, 64, stdin);

		if (cmnd[0] == '\n' || cmnd[0] == '\r')
			continue;

		if (strstr(cmnd, "HELP") != NULL)
		{
			int len = strlen(cmnd);
			cmnd[len - 1] = 0;
			snprintf(cmnd, 64, "%s\r\n", cmnd);

			ftpSendCommand(cmnd);
			Sleep(1);
			ftpRecieveRespond(responseBuffer, 1024);
			SecureZeroMemory(cmnd, sizeof(cmnd));
			SecureZeroMemory(responseBuffer, sizeof(responseBuffer));
		}
		else if(strcmp(cmnd, "LIST\n") == 0)
		{
			char pasvIpAddress[16];
			int pasvPort;
			if (ftpEnterPasv(pasvIpAddress, &pasvPort) != 0)
			{
				printf("Data stream creation error!\n");
				ftpQuit();
				return 0;
			}

			int len = strlen(cmnd);
			cmnd[len - 1] = 0;
			snprintf(cmnd, 64, "%s\r\n", cmnd);

			HANDLE hDataStreamThread;
			LPVOID params = createDataStreamThread(pasvIpAddress, pasvPort, &hDataStreamThread);

			ftpSendCommand(cmnd);

			WaitForSingleObject(hDataStreamThread, INFINITE);

			ftpRecieveRespond(responseBuffer, 1024);
			SecureZeroMemory(cmnd, sizeof(cmnd));
			SecureZeroMemory(responseBuffer, sizeof(responseBuffer));

			CloseHandle(hDataStreamThread);
			free(params);
		}
		else if (strcmp(cmnd, "QUIT\n") == 0)
		{
			int len = strlen(cmnd);
			cmnd[len - 1] = 0;
			snprintf(cmnd, 64, "%s\r\n", cmnd);

			ftpSendCommand(cmnd);
			Sleep(1);
			ftpRecieveRespond(responseBuffer, 1024);
			SecureZeroMemory(cmnd, sizeof(cmnd));
			SecureZeroMemory(responseBuffer, sizeof(responseBuffer));
			break;
		}
		else
		{
			int len = strlen(cmnd);
			cmnd[len - 1] = 0;

			printf("\"%s\" is not implemented yet!\n", cmnd);
			SecureZeroMemory(cmnd, sizeof(cmnd));
		}
	}

	ftpQuit();
	system("pause");
	return 0;
}

static SOCKET socketCreate()
{
	return socket(AF_INET, SOCK_STREAM, 0);
}

static int socketConnect(SOCKET s, char *addr, int port)
{
	struct sockaddr_in server;

	SecureZeroMemory(&server, sizeof(server));
	server.sin_addr.s_addr = inet_addr(addr);
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if (connect(s, (struct sockaddr *)&server, sizeof(server)), 0)
	{
		puts("Connect failed!");
		return 0;
	}
	return 1;
}

static int ftpRecieveRespond(char *responseBuffer, int sizeOfResponseBufferint)
{
	int ret;
	int off;
	//sizeOfResponseBufferint -= 1;
	for (off = 0; off < sizeOfResponseBufferint; )
	{
		ret = recv(commandSocket, &responseBuffer[off], sizeOfResponseBufferint, 0);
		off += ret;
		if (ret < 0)
		{
			printf_s("recv respond error (error code = %d)!\r\n", ret);
			system("pause");
			return 0;
		}
		if (responseBuffer[off] == 0)
		{
			break;
		}
	}
	//responseBuffer[off + 1] = 0;
	printf_s("Server response: %s", responseBuffer);
	ret = atoi(responseBuffer);
	//SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);
	return ret;
}

static int ftpSendCommand(char *cmd)
{
	int ret;
	printf_s("Sending command: %s\r\n", cmd);
	ret = send(commandSocket, cmd, (int)strlen(cmd), 0);
	if (ret < 0)
	{
		printf_s("Failed to send command: %s\n", cmd);
		system("pause");
		return 0;
	}

	return 1;
}

static int ftpEnterPasv(char *ipaddr, int *port)
{
	int ret;
	char *find;
	int a, b, c, d;
	int pa, pb;
	ret = ftpSendCommand("PASV\r\n");
	if (ret != 1)
	{
		return 1;
	}
	ret = ftpRecieveRespond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 227)
	{
		return 1;
	}
	find = strrchr(recvBuffer, '(');
	sscanf_s(find, "(%d,%d,%d,%d,%d,%d)", &a, &b, &c, &d, &pa, &pb);
	sprintf(ipaddr, "%d.%d.%d.%d", a, b, c, d);
	*port = pa * 256 + pb;
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);
	return 0;
}

static int ftpLogin(char *addr, int port, char *username, char *password)
{
	int ret;
	printf_s("Connecting...\r\n");
	ret = socketConnect(commandSocket, addr, port);
	if (ret != 1)
	{
		printf_s("Connection to server failed!\r\n");
		return 1;
	}
	printf_s("Connected!\r\n");
	//Waiting for Welcome Message
	ret = ftpRecieveRespond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 220)
	{
		printf_s("Bad server, (error code = %d)!\r\n", ret);
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	printf_s("Logging in...\r\n");
	//Send USER
	sprintf_s(sendBuffer, "USER %s\r\n", username);
	ret = ftpSendCommand(sendBuffer);
	if (ret != 1)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	ret = ftpRecieveRespond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 331)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	//Send PASS
	sprintf_s(sendBuffer, "PASS %s\r\n", password);
	ret = ftpSendCommand(sendBuffer);
	if (ret != 1)
	{
		closesocket(commandSocket);
		return 1;
	}
	ret = ftpRecieveRespond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 230)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);
	printf_s("login success.\r\n");

	//Set to binary mode
	ret = ftpSendCommand("TYPE I\r\n");
	if (ret != 1)
	{
		closesocket(commandSocket);
		return 1;
	}
	ret = ftpRecieveRespond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 200)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	return 0;
}

static void ftpQuit()
{
	closesocket(commandSocket);
	WSACleanup();
}

static int ftpInit()
{
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed to initiazlie WinSock. Error Code : %d\n", WSAGetLastError());
		return 1;
	}

	commandSocket = socketCreate();
	if (commandSocket == INVALID_SOCKET)
		printf_s("Socket creation failed!\n");
}

static int ftpSetUpDataSocket(SOCKET *s, char *ipAddress, int port)
{
	*s = socketCreate();
	if (*s == INVALID_SOCKET)
		printf_s("Socket creation failed!\n");

	sockaddr_in server;
	server.sin_addr.s_addr = inet_addr(ipAddress);
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if (connect(*s, (struct sockaddr *)&server, sizeof(server)) != 0)
	{
		puts("Connection failed!\n");
		return 0;
	}

	return 1;
}

DWORD WINAPI ftpHandleDataStream(LPVOID lpParam)
{
	char ipAddress[16];
	int port;
	CopyMemory(ipAddress, lpParam, sizeof(char) * 16);
	CopyMemory(&port, (char*)lpParam + 16, sizeof(int));

	printf("Listening on %s:%d\n", ipAddress, port);

	SOCKET data;
	ftpSetUpDataSocket(&data, ipAddress, port);

	char inputBuffer[1024];
	int recv_size = recv(data, inputBuffer, 1024, 0);
	inputBuffer[recv_size] = 0;
	printf("%s\n", inputBuffer);

	closesocket(data);
}

static LPVOID createDataStreamThread(char *pasvIpAddress, int pasvPort, HANDLE *hDataStreamThread)
{
	LPVOID params = malloc(sizeof(char) * 16 + sizeof(int));
	CopyMemory(params, pasvIpAddress, sizeof(char) * 16);
	CopyMemory((char*)params + 16, &pasvPort, sizeof(int));

	DWORD dwHThreadId; 
	*hDataStreamThread = CreateThread(NULL, 0, ftpHandleDataStream, params, 0, &dwHThreadId);

	return params;
}