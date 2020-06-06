#include <stdio.h>
#include <winsock2.h>
#include <locale.h>

#pragma comment(lib,"ws2_32.lib")

#define SEND_BUFFER_LENGTH 1024
#define RECV_BUFFER_LENGTH 1024

static WSADATA wsa;
static SOCKET commandSocket;
static SOCKET dataSocket;

static char sendBuffer[SEND_BUFFER_LENGTH];
static char recvBuffer[RECV_BUFFER_LENGTH];

static SOCKET socketCreate();
static int socketConnect(SOCKET s, char *addr, int port);

static int ftp_send_command(char *cmd);
static int ftp_recv_respond(char *responseBuffer, int sizeOfResponseBufferint);
static int ftp_enter_pasv(char *ipaddr, int *port);
static int ftp_login(char *addr, int port, char *username, char *password);
static void ftp_quit();
static int ftp_init();

static int ftpSetUpDataSocket(SOCKET *s, char *ipAddress, int port);
DWORD WINAPI ftpHandleDataStream(LPVOID lpParam);
static LPVOID createDataStreamThread(char *pasvIpAddress, int pasvPort, HANDLE *hDataStreamThread);

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "Russian");

	char ipAdressStr[16], loginStr[64], passwordStr[256];
	int port;
	printf("¬ведите IP-адрес сервера: ");
	scanf("%s", ipAdressStr);
	printf("¬ведите порт сервера: ");
	scanf("%d", &port);
	printf("¬ведите логин: ");
	scanf("%s", loginStr);
	printf("¬ведите пароль: ");
	scanf("%s", passwordStr);

	ftp_init();
	if (ftp_login(ipAdressStr, port, loginStr, passwordStr) != 0)
	{
		printf("Login error!\n");
		ftp_quit();
		return 0;
	}

	char cmnd[64];
	char responseBuffer[1024];
	SecureZeroMemory(responseBuffer, 1024);
	while (true)
	{
		char pasvIpAddress[16];
		int pasvPort;
		if (ftp_enter_pasv(pasvIpAddress, &pasvPort) != 0)
		{
			printf("Data stream creation error!\n");
			ftp_quit();
			return 0;
		}

		scanf("%s", cmnd);
		snprintf(cmnd, 64, "%s\r\n", cmnd);

		if (strcmp(cmnd, "HELP\r\n") == 0)
		{
			ftp_send_command(cmnd);
			Sleep(10);
			ftp_recv_respond(responseBuffer, 1024);
			SecureZeroMemory(cmnd, sizeof(cmnd));
			SecureZeroMemory(responseBuffer, sizeof(responseBuffer));
		}
		else
		{
			HANDLE hDataStreamThread;
			LPVOID params = createDataStreamThread(pasvIpAddress, pasvPort, &hDataStreamThread);

			ftp_send_command(cmnd);

			WaitForSingleObject(hDataStreamThread, INFINITE);

			ftp_recv_respond(responseBuffer, 1024);
			SecureZeroMemory(cmnd, sizeof(cmnd));
			SecureZeroMemory(responseBuffer, sizeof(responseBuffer));

			CloseHandle(hDataStreamThread);
			free(params);
		}
	}

	ftp_quit();
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
		puts("connect error");
		return 0;
	}
	return 1;
}

static int ftp_recv_respond(char *responseBuffer, int sizeOfResponseBufferint)
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
			printf_s("recv respond error(ret=%d)!\r\n", ret);
			system("pause");
			return 0;
		}
		if (responseBuffer[off] == 0)
		{
			break;
		}
	}
	//responseBuffer[off + 1] = 0;
	printf_s("respond: %s", responseBuffer);
	ret = atoi(responseBuffer);
	//SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);
	return ret;
}

static int ftp_send_command(char *cmd)
{
	int ret;
	printf_s("send command: %s\r\n", cmd);
	ret = send(commandSocket, cmd, (int)strlen(cmd), 0);
	if (ret < 0)
	{
		printf_s("failed to send command: %s", cmd);
		system("pause");
		return 0;
	}

	return 1;
}

static int ftp_enter_pasv(char *ipaddr, int *port)
{
	int ret;
	char *find;
	int a, b, c, d;
	int pa, pb;
	ret = ftp_send_command("PASV\r\n");
	if (ret != 1)
	{
		return 1;
	}
	ret = ftp_recv_respond(recvBuffer, RECV_BUFFER_LENGTH);
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

static int ftp_login(char *addr, int port, char *username, char *password)
{
	int ret;
	printf_s("connect...\r\n");
	ret = socketConnect(commandSocket, addr, port);
	if (ret != 1)
	{
		printf_s("connect server failed!\r\n");
		return 1;
	}
	printf_s("connect ok.\r\n");
	//Waiting for Welcome Message
	ret = ftp_recv_respond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 220)
	{
		printf_s("bad server, ret=%d!\r\n", ret);
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	printf_s("login...\r\n");
	//Send USER
	sprintf_s(sendBuffer, "USER %s\r\n", username);
	ret = ftp_send_command(sendBuffer);
	if (ret != 1)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	ret = ftp_recv_respond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 331)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	//Send PASS
	sprintf_s(sendBuffer, "PASS %s\r\n", password);
	ret = ftp_send_command(sendBuffer);
	if (ret != 1)
	{
		closesocket(commandSocket);
		return 1;
	}
	ret = ftp_recv_respond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 230)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);
	printf_s("login success.\r\n");

	//Set to binary mode
	ret = ftp_send_command("TYPE I\r\n");
	if (ret != 1)
	{
		closesocket(commandSocket);
		return 1;
	}
	ret = ftp_recv_respond(recvBuffer, RECV_BUFFER_LENGTH);
	if (ret != 200)
	{
		closesocket(commandSocket);
		return 1;
	}
	SecureZeroMemory(recvBuffer, RECV_BUFFER_LENGTH);

	return 0;
}

static void ftp_quit()
{
	ftp_send_command("QUIT\r\n");
	closesocket(commandSocket);
	closesocket(dataSocket);
	WSACleanup();
}

static int ftp_init()
{
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		return 1;
	}

	commandSocket = socketCreate();
	if (commandSocket == INVALID_SOCKET)
		printf_s("Socket creation failed!\n");
	dataSocket = socketCreate();
	if (dataSocket == INVALID_SOCKET)
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
		puts("connect error");
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

	printf("Listening in %s:%d\n", ipAddress, port);

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