// SimpleFastSocketClient.cpp : �ܼ� ���� ���α׷��� ���� �������� �����մϴ�.
//

#include "stdafx.h"
#include <iostream>
#include "SimpleClient.h"


int _tmain(int argc, _TCHAR* argv[])
{
	SimpleClient Client;
	Client.SocketInit();

	if( NETWORK_ERROR_NONE != Client.Init( false, 1, 16840 ) )
	{
		std::cout << "ERROR - ���� �ʱ�ȭ ����!!!" << std::endl;
		return 0;
	}

	char szIP[64] = "127.0.0.1";
	unsigned short nPort = 10101;
	std::cout << "Server IP : " << szIP << std::endl;
	std::cout << "Server Port : " << nPort << std::endl;

	unsigned short nRet = Client.Connect( szIP, nPort );
	if( NETWORK_ERROR_NONE == nRet )
	{
		std::cout << "Ŭ���̾�Ʈ ���� ����" << std::endl;
		SimpleClient::NetworkProc( &Client );
	}
	else
	{
		std::cout << "Ŭ���̾�Ʈ ���� ���� : " << nRet << std::endl;
	}

	Client.SocketTerminate();
	return 0;
}