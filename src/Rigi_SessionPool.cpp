#include "Rigi_SessionPool.hpp"
#include "Rigi_Server.hpp"

using namespace Rigitaeda;

Rigi_SessionPool::Rigi_SessionPool()
{
	m_nReceive_Packet_Size = 10240;
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
	// 미리 호출해서 접속한 클라이언트 아이피를 저장하자
	// 여기서 호출하지 않으면 close 때 호출이 되는데 이때는 값이 없다.
	_pSession->SetSocket(_pSocket);
	std::string strClientIP = _pSession->Get_SessionIP();

	auto find = m_mapTCP.find(_pSession);
	if(find == m_mapTCP.end())
	{
		// 버퍼를 Receive 전에 해줘야 한다
		_pSession->Make_Receive_Packet_Size(m_nReceive_Packet_Size);

		if (m_nMaxClient < (int)m_mapTCP.size())
		{
			char szClose[] = "Connection Full (";
			std::cout << "[ACCEPT] >> " << szClose << strClientIP << ")" << std::endl;
			_pSession->Sync_Send(szClose, sizeof(szClose));
			_pSession->Close();
			return false;
		}
		else
		{
			m_mapTCP.insert( std::make_pair(_pSession, _pSession) );

			_pSession->SetSessionPool(this);
			// _pSession->Add_Event_Handler_Close( m_pRigi_Server->Get_Event_Handler_Close() );
			// _pSession->Add_Event_Handler_Init( m_pRigi_Server->Get_Event_Handler_Init() );
			// _pSession->Add_Event_Handler_Receive( m_pRigi_Server->Get_Event_Handler_Receive() );
			// _pSession->Add_Event_Handler_Send( m_pRigi_Server->Get_Event_Handler_Send() );
			_pSession->Add_Event_Handler_Close( std::move(m_pRigi_Server->m_Func_Event_Close) );
			_pSession->Add_Event_Handler_Init( std::move(m_pRigi_Server->m_Func_Event_Init) );
			_pSession->Add_Event_Handler_Receive( std::move(m_pRigi_Server->m_Func_Event_Receive) );
			_pSession->Add_Event_Handler_Send( std::move(m_pRigi_Server->m_Func_Event_Send) );

			// Init 이벤트 호출
			if( false == _pSession->OnEvent_Init() )
			{
				//ASSERT(0 && "[Rigi_SessionPool::Add_Session] OnEvent_Init is false !!");
				return false;
			}

			_pSession->Async_Receive();
			return true;
		}
	}
	else
		return false;
}

bool Rigi_SessionPool::Close_Session( __in Rigi_TCPSession *_pSession )
{
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
	for(auto &sock : m_mapTCP)
	{
		sock.second->Close();
		delete sock.second;
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