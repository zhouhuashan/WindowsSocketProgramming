#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")


#include <vector>
#include <deque>

const INT32 MAX_SPIN_LOCK_COUNT = 4000;
const int SOCKET_INDEX_LISTEN = 0;

// [�����ڵ�]
const UINT16 NETWORK_ERROR_NONE = 0;
const UINT16 NETWORK_ERROR_INVALID_SOCKET_COUNT = 1;
const UINT16 NETWORK_ERROR_FAIL_SOCKET_INIR = 2;
const UINT16 NETWORK_ERROR_CONNECT_FULL_SOCKET_OBJECT = 3;
const UINT16 NETWORK_ERROR_CONNECT_FAILED_CONNECT = 4;
const UINT16 NETWORK_ERROR_CONNECT_FAILED_NEW_EVENT = 5;

#pragma pack(1)
// ��Ŷ ���
struct PACKET_HEADER
{
	unsigned short usBodySize;
	unsigned short usType;
};
#pragma pack()

// ��Ŷ ��� ũ��
const int PACKET_HEADER_SIZE = sizeof(PACKET_HEADER);

// ��Ŷ Ŀ�ǵ�
struct PacketCMD
{
	PacketCMD() { pData = NULL;}
	~PacketCMD() { if( NULL != pData) {delete[] pData;} }

	void SetData( unsigned short nType, unsigned short nPacketSize, char* pPacketData )
	{
		nCmdType = nType;
		nDataSize = nPacketSize;

		if( nDataSize > 0 )
		{
			pData = new char[nDataSize];
			CopyMemory(pData, pPacketData, nDataSize);
		}
	}
		
	unsigned short nCmdType;	// ��Ŷ �ε���
	unsigned short nDataSize;	// ��Ŷ ��ü ũ��
	char* pData;				// ��Ŷ ������
};

// ���� ��ü 
struct SOCKETOBJECT
{
	~SOCKETOBJECT()
	{
		delete[] pBuffer;
	}

	void Init( int nSeq, int nBufferSize )
	{
		this->nIndex = nSeq;
		this->nBufferSize = nBufferSize * 2;
		pBuffer = new char[this->nBufferSize];
		Clear();
	}
	void Clear()
	{
		bIsUsed = false;
		socket = INVALID_SOCKET;
		nCurWritePos = 0;
		ZeroMemory( pBuffer, nBufferSize );
	}
	void SetSocket( SOCKET sock )
	{
		socket = sock;
		bIsUsed = true;
	}
	int nIndex;		// ���� �ε���
	bool bIsUsed;	// ��� ����
	SOCKET socket;	// ���� 
	
	char *pBuffer;		// ����(�ޱ��)
	int nBufferSize;	// ������ ũ��
	int nCurWritePos;	// ���ۿ� ���� ��ġ
};


class TcpSocket
{
public:
	TcpSocket()
	{
		InitializeCriticalSectionAndSpinCount(&m_CmdLock, MAX_SPIN_LOCK_COUNT);
	}

	virtual ~TcpSocket()
	{
		SocketTerminate();

		int nCount = (int)m_SockObjList.size();
		for (int i = 0; i < nCount; ++i)
		{
			delete m_SockObjList[i];
		}
		m_SockObjList.clear();


		nCount = (int)m_Commands.size();
		for (int i = 0; i < nCount; ++i)
		{
			delete m_Commands[i];
		}
		m_Commands.clear();

		DeleteCriticalSection(&m_CmdLock);
	}
		
	// �ʱ�ȭ
	unsigned short Init(const bool bIsServer, const int nMaxSocket, const int nBufferSize)
	{
		m_bIsServer = bIsServer;

		m_nMaxSocketCount = nMaxSocket + 2;
		if (m_nMaxSocketCount < 3 || m_nMaxSocketCount > WSA_MAXIMUM_WAIT_EVENTS) {
			return NETWORK_ERROR_INVALID_SOCKET_COUNT;
		}

		for (int i = 0; i < m_nMaxSocketCount; ++i)
		{
			SOCKETOBJECT* pConnObject = new SOCKETOBJECT;
			pConnObject->Init(i, nBufferSize);
			m_SockObjList.push_back(pConnObject);
		}

		m_bNetworkProcessing = true;
		return NETWORK_ERROR_NONE;
	}

	// Windows Socket �ʱ�ȭ
	unsigned short SocketInit()
	{
		WSADATA wsaData = { 0 };
		m_nSocketInitResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (m_nSocketInitResult != 0) {
			return NETWORK_ERROR_FAIL_SOCKET_INIR;
		}

		return NETWORK_ERROR_NONE;
	}

	// Windows Socket ����
	void SocketTerminate()
	{
		m_bNetworkProcessing = false;
		if (INVALID_SOCKET != m_ListenSocket) {
			closesocket(m_ListenSocket);
		}

		if (0 == m_nSocketInitResult) {
			WSACleanup();
		}
	}
	
	// ���� �̺�Ʈ ó��
	static unsigned short NetworkProc(TcpSocket* pObject)
	{
		while (pObject->m_bNetworkProcessing)
		{
			if (false == pObject->m_bNetworkProcessing) {
				break;
			}

			DWORD Index = WSAWaitForMultipleEvents(pObject->m_nUseEventCount, pObject->m_EventArray, FALSE, WSA_INFINITE, FALSE);
			Index = Index - WSA_WAIT_EVENT_0;

			for (int i = 0; i < pObject->m_nUseEventCount; ++i)
			{
				Index = WSAWaitForMultipleEvents(1, &pObject->m_EventArray[i], TRUE, 5, FALSE);
				if (Index == WSA_WAIT_FAILED || Index == WSA_WAIT_TIMEOUT) {
					continue;
				}

				Index = i - WSA_WAIT_EVENT_0;
				WSANETWORKEVENTS NetworkEvents;
				WSAEnumNetworkEvents(pObject->m_SocketArray[Index], pObject->m_EventArray[Index], &NetworkEvents);

				if (pObject->m_bIsServer && 0 == Index && NetworkEvents.lNetworkEvents && FD_ACCEPT)
				{
					if (NetworkEvents.iErrorCode[FD_ACCEPT_BIT] != 0)
					{
						break;
					}
					pObject->OnAccept(Index);
				}

				/// READ
				if (NetworkEvents.lNetworkEvents & FD_READ)
				{
					if (NetworkEvents.iErrorCode[FD_READ_BIT] != 0)
					{
						break;
					}

					pObject->OnRead(Index);
				}

				/// Close
				if (NetworkEvents.lNetworkEvents & FD_CLOSE)
				{
					/*if( NetworkEvents.iErrorCode[FD_CLOSE_BIT] != 0 )
					{
					break;
					}*/

					pObject->OnClose(Index);
				}
			}


		}
		return NETWORK_ERROR_NONE;
	}

	// ��Ʈ���� ���� �̺�Ʈ ó��(ȣ��Ʈ��)
	virtual int OnAccept(const int nIndex)
	{
		SOCKET NewSocket = accept(m_SocketArray[nIndex], NULL, NULL);
		SOCKETOBJECT* pSockObj = GetUsableSocketObject();
		if (NULL == pSockObj)
		{
			closesocket(NewSocket);
			return -1;
		}

		WSAEVENT NewEvent = WSACreateEvent();

		WSAEventSelect(NewSocket, NewEvent, FD_READ | FD_CLOSE);

		AddEvent(NewSocket, NewEvent);

		pSockObj->SetSocket(NewSocket);
		return pSockObj->nIndex;
	}

	// ��Ʈ���� �б� �̺�Ʈ ó��
	virtual int OnRead(const int nIndex)
	{
		SOCKET socket = m_SocketArray[nIndex];
		SOCKETOBJECT* pSockObj = GetSocketObject(socket);
		if (NULL == pSockObj) {
			return -1;
		}

		int nReadSize = recv(pSockObj->socket, pSockObj->pBuffer, pSockObj->nBufferSize, pSockObj->nCurWritePos);

		int nBuffPos = 0;
		const int nLastPos = pSockObj->nCurWritePos + nReadSize;
		bool bIsLoop = true;
		while (bIsLoop)
		{
			bool bIsSmall = false;
			int nRemainSize = nLastPos - nBuffPos;

			if (nRemainSize < 0) {
				pSockObj->nCurWritePos = 0;
				break;
			}

			if ((nRemainSize < PACKET_HEADER_SIZE))
			{
				bIsSmall = true;
			}

			PACKET_HEADER* pHeader = NULL;
			if (false == bIsSmall)
			{
				pHeader = (PACKET_HEADER*)&pSockObj->pBuffer[nBuffPos];
				if ((pHeader->usBodySize + PACKET_HEADER_SIZE) < nRemainSize) {
					bIsSmall = true;
				}
			}


			if (bIsSmall)
			{
				CopyMemory(&pSockObj->pBuffer[0], &pSockObj->pBuffer[nBuffPos], nRemainSize);
				pSockObj->nCurWritePos = nRemainSize;
				break;
			}

			//
			char* pData = (char*)&pSockObj->pBuffer[nBuffPos];
			PushCommand(pHeader->usType, pHeader->usBodySize + PACKET_HEADER_SIZE, pData);

			nBuffPos += (pHeader->usBodySize + PACKET_HEADER_SIZE);
		}

		return pSockObj->nIndex;
	}

	// ��Ʈ���� ������ �̺�Ʈ ó��
	virtual int OnClose(const int nIndex)
	{
		SOCKET closeSocket = m_SocketArray[nIndex];
		SOCKETOBJECT* pSockObj = GetSocketObject(closeSocket);
		if (NULL == pSockObj) {
			return -1;
		}
		int nSockObjIndex = pSockObj->nIndex;

		closesocket(m_SocketArray[nIndex]);
		WSACloseEvent(m_EventArray[nIndex]);

		RemoveEvent(nIndex);

		SetUnUseSocketObject(closeSocket);
		return nSockObjIndex;
	}
	
	

	// ��Ŷ ������
	bool Send(const int nIndex, unsigned short nSize, char* pData)
	{
		SOCKETOBJECT* pSockObj = GetSocketObject(nIndex);
		if (NULL == pSockObj || INVALID_SOCKET == pSockObj->socket) {
			return false;
		}

		send(pSockObj->socket, pData, nSize, 0);
		return true;
	}
	
	// ����Ʈ ����
	void Close(SOCKET socket)
	{
		shutdown(socket, SD_BOTH);
		closesocket(socket);
	}

	// ��Ŷ�� ���� ��ü�� �ְ� ����
	void PushCommand(unsigned short nType, unsigned short nSize, char* pData)
	{
		PacketCMD* pCmd = new PacketCMD;
		pCmd->SetData(nType, nSize, pData);

		EnterCriticalSection(&m_CmdLock);
		m_Commands.push_back(pCmd);
		LeaveCriticalSection(&m_CmdLock);
	}

	PacketCMD *PopCommand()
	{
		PacketCMD* pCmd = NULL;

		EnterCriticalSection(&m_CmdLock);
		if (false == m_Commands.empty())
		{
			pCmd = m_Commands[0];
			m_Commands.pop_front();
		}
		LeaveCriticalSection(&m_CmdLock);

		return pCmd;
	}

	// ���� ���� -  SEFVER�� ȣ�� ����
	unsigned short StartServer(unsigned short nPort)	
	{
		m_nPort = nPort;
		m_ListenSocket = socket(PF_INET, SOCK_STREAM, 0);

		SOCKADDR_IN InternetAddr;
		InternetAddr.sin_family = AF_INET;
		InternetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		InternetAddr.sin_port = htons(m_nPort);

		bind(m_ListenSocket, (PSOCKADDR)&InternetAddr, sizeof(InternetAddr));
		WSAEVENT NewEvent = WSACreateEvent();
		WSAEventSelect(m_ListenSocket, NewEvent, FD_ACCEPT | FD_CLOSE);

		listen(m_ListenSocket, 5);

		AddEvent(m_ListenSocket, NewEvent);

		return NETWORK_ERROR_NONE;
	}
	
	// �����ϱ�(�ַ� Ŭ���̾�Ʈ�� ���)
	unsigned short Connect(const char* pszIP, const unsigned short nPort)
	{
		SOCKETOBJECT* pSockObj = GetUsableSocketObject();
		if (NULL == pSockObj) {
			return NETWORK_ERROR_CONNECT_FULL_SOCKET_OBJECT;
		}

		SOCKET NewSocket = socket(AF_INET, SOCK_STREAM, 0);

		SOCKADDR_IN addrToServer;
		ZeroMemory(&addrToServer, sizeof(addrToServer));
		addrToServer.sin_addr.s_addr = ::inet_addr(pszIP);
		addrToServer.sin_family = AF_INET;
		addrToServer.sin_port = ::htons(nPort);

		if (SOCKET_ERROR == connect(NewSocket, (struct sockaddr*)&addrToServer, sizeof(addrToServer))) {
			return NETWORK_ERROR_CONNECT_FAILED_CONNECT;
		}

		WSAEVENT NewEvent = WSACreateEvent();
		if (SOCKET_ERROR == WSAEventSelect(NewSocket, NewEvent, FD_READ | FD_CLOSE)) {
			return NETWORK_ERROR_CONNECT_FAILED_NEW_EVENT;
		}


		AddEvent(NewSocket, NewEvent);

		pSockObj->SetSocket(NewSocket);

		return NETWORK_ERROR_NONE;
	}


protected:
	// ����� �� �ִ� ���� ������Ʈ ���
	SOCKETOBJECT * GetUsableSocketObject()				
	{
		int nCount = (int)m_SockObjList.size();
		for (int i = 0; i < nCount; ++i)
		{
			if (false == m_SockObjList[i]->bIsUsed) {
				return m_SockObjList[i];
			}
		}

		return NULL;
	}

	// ���ϰ� ���õ� ���� ������Ʈ �� ������� ����
	void SetUnUseSocketObject(const SOCKET sock)			
	{
		SOCKETOBJECT* pSockObj = GetSocketObject(sock);
		if (NULL == pSockObj) {
			return;
		}

		pSockObj->Clear();
	}

	// �������� ���� ������Ʈ ã��
	SOCKETOBJECT* GetSocketObject(const SOCKET sock)
	{
		int nIndex = -1;
		int nCount = (int)m_SockObjList.size();
		for (int i = 0; i < nCount; ++i)
		{
			if (m_SockObjList[i]->bIsUsed && sock == m_SockObjList[i]->socket)
			{
				nIndex = i;
				break;
			}
		}

		if (-1 == nIndex) {
			return NULL;
		}

		return m_SockObjList[nIndex];
	}

	// �ε����� ���� ������Ʈ ã��
	SOCKETOBJECT* GetSocketObject(const int nIndex)
	{
		int nCount = (int)m_SockObjList.size();
		if (nIndex < 0 || nIndex >= nCount) {
			return NULL;
		}

		return m_SockObjList[nIndex];
	}

	// ���� �̺�Ʈ �߰� �� ����
	void AddEvent(SOCKET sock, WSAEVENT event)
	{
		m_SocketArray[m_nUseEventCount] = sock;
		m_EventArray[m_nUseEventCount] = event;
		++m_nUseEventCount;
	}

	void RemoveEvent(const int nIndex)
	{
		for (int i = nIndex; i < m_nUseEventCount; ++i)
		{
			m_SocketArray[i] = m_SocketArray[i + 1];
			m_EventArray[i] = m_EventArray[i + 1];
		}
		--m_nUseEventCount;
	}

	


	std::vector< SOCKETOBJECT* > m_SockObjList;		// ���� ������Ʈ ����Ʈ
	int m_nUseEventCount = 0;							// ���� ����ϴ� �� �̺�Ʈ ����
	SOCKET m_SocketArray[WSA_MAXIMUM_WAIT_EVENTS];	// ���� �迭
	WSAEVENT m_EventArray[WSA_MAXIMUM_WAIT_EVENTS];	// �̺�Ʈ �迭
	SOCKET m_ListenSocket = INVALID_SOCKET;							// listen ����
	
	bool m_bIsServer = false;								// ���� ����
	int m_nMaxSocketCount;							// �� ��� ���� ���� ����
	unsigned short m_nPort = 0;							// ��Ʈ ��ȣ
	bool m_bNetworkProcessing = false;						// ��Ʈ�� ó�� ���� ����	

	int m_nSocketInitResult = -1;						// ���� �ʱ�ȭ ���
	
	CRITICAL_SECTION m_CmdLock;						// ��Ŷ Ŀ�ǵ�� �� ��ü
	std::deque<PacketCMD*> m_Commands;				// ��Ŷ Ŀ�ǵ� �����̳�
};