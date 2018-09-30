// SimpleFastSocketServer.cpp : �ܼ� ���� ���α׷��� ���� �������� �����մϴ�.
//

#include "stdafx.h"
#include <iostream>
#include "SimpleServer.h"


int _tmain(int argc, _TCHAR* argv[])
{
	SimpleServer Server;
	Server.SocketInit();

	if( NETWORK_ERROR_NONE != Server.Init( true, 2, 16840 ) )
	{
		std::cout << "ERROR - ���� �ʱ�ȭ ����!!!" << std::endl;
		return 0;
	}

	Server.StartServer( 10101 );
	std::cout << "���� ����!!!" << std::endl;
	SimpleServer::NetworkProc( &Server );


	Server.SocketTerminate();
	return 0;
}
