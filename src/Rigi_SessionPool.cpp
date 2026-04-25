#include "Rigi_SessionPool.hpp"
#include "Rigi_Server.hpp"

using namespace Rigitaeda;

Rigi_SessionPool::Rigi_SessionPool()
{
	m_nMaxClient = 0;
	m_nReceive_Packet_Size = 10240;
	m_pRigi_Server = nullptr;
}

Rigi_SessionPool::~Rigi_SessionPool()
{
	Clear();
}

void Rigi_SessionPool::Set_MaxClient( __in int _nMaxClient )
{
	m_nMaxClient = _nMaxClient;
}

bool Rigi_SessionPool::Add_Session( __in Rigi_TCPSession *_pSession,
									__in SOCKET_TCP *_pSocket )
{
	// 소켓을 먼저 연결해야 Get_SessionIP()가 정상 동작함
	_pSession->SetSocket(_pSocket);
	std::string strClientIP = _pSession->Get_SessionIP();

	std::lock_guard<std::mutex> lock(m_mutex);
	auto find = m_mapTCP.find(_pSession);
	if(find == m_mapTCP.end())
	{
		_pSession->Make_Receive_Packet_Size(m_nReceive_Packet_Size);

		if (m_nMaxClient < (int)m_mapTCP.size())
		{
			char szClose[] = "Connection Full";
			std::cout << "[ACCEPT] >> " << szClose << " (" << strClientIP << ")" << std::endl;
			_pSession->Sync_Send(szClose, sizeof(szClose));
			_pSession->Close();
			return false;
		}
		else
		{
			m_mapTCP.insert( std::make_pair(_pSession, _pSession) );

			_pSession->SetSessionPool(this);
			// std::move 대신 복사: move하면 두 번째 접속부터 핸들러가 빈 함수가 됨
			_pSession->Add_Event_Handler_Close( m_pRigi_Server->m_Func_Event_Close );
			_pSession->Add_Event_Handler_Init( m_pRigi_Server->m_Func_Event_Init );
			_pSession->Add_Event_Handler_Receive( m_pRigi_Server->m_Func_Event_Receive );
			_pSession->Add_Event_Handler_Send( m_pRigi_Server->m_Func_Event_Send );

			if( false == _pSession->OnEvent_Init() )
				return false;

			_pSession->Async_Receive();
			return true;
		}
	}
	else
		return false;
}

bool Rigi_SessionPool::Close_Session( __in Rigi_TCPSession *_pSession )
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto find = m_mapTCP.find(_pSession);
	if(find == m_mapTCP.end())
		return false;

	delete find->second;
	m_mapTCP.erase(find);

	return true;
}

// void Rigi_SessionPool::Add_Session( __in Rigi_UDPSession *_pSession )
// {
// 	auto find = m_mapUDP.find(_pSession);
// 	if(find == m_mapUDP.end())
// 	{
// 		m_mapUDP.insert( std::make_pair(_pSession, _pSession) );

// 		return true;
// 	}

// 	return false;
// }

// void Rigi_SessionPool::Close_Session( __in Rigi_UDPSession *_pSession )
// {
// 	auto find = m_mapUDP.find(_pSession);
// 	if(find == m_mapUDP.end())
// 		return false;

// 	delete find.second;
// 	m_mapUDP.erase(find);

// 	return true;
// }

void Rigi_SessionPool::Clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for(auto &kv : m_mapTCP)
	{
		Rigi_TCPSession* session = kv.second;
		// pool 포인터를 먼저 null로 만들어 Close()가 Close_Session()을 재호출하지 않도록 한다
		session->m_pSessionPool = nullptr;
		session->Close();
		delete session;
	}
	m_mapTCP.clear();

	// for(auto &sock : m_mapUDP)
	// 	delete sock.second;
	// m_mapUDP.clear();
}

void Rigi_SessionPool::Set_Receive_Packet_Size( __in int _nReceive_Packet_Size )
{
	m_nReceive_Packet_Size = _nReceive_Packet_Size;
}