#include "Rigi_TCPSession.hpp"
#include "Rigi_TCPServerMgr.hpp"
#include "Rigi_SessionPool.hpp"

using namespace Rigitaeda;

#define CATCH_EXCEPTION()	\
catch(const std::exception &e) \
{ \
	std::cout << "[Exception][" << __FUNCTION__ << "] Error = " << e.what() << std::endl; \
} \
catch(...) \
{ \
	std::cout << "[Exception][" << __FUNCTION__ << "] Error !!" << std::endl; \
} \

Rigi_TCPSession::Rigi_TCPSession()
{
	m_pSocket = nullptr;
	m_pTCPMgr = nullptr;
	m_pSessionPool = nullptr;
	m_pReceive_Packet_Buffer = nullptr;
	m_nReceive_Packet_Size = 0;

	m_Func_Event_Close = nullptr;
	m_Func_Event_Init = nullptr;
	m_Func_Event_Receive = nullptr;
	m_Func_Event_Send = nullptr;
}

Rigi_TCPSession::~Rigi_TCPSession()
{
	Close(boost::asio::error::eof);

	m_pSocket = nullptr;

	if ( nullptr != m_pReceive_Packet_Buffer )
		delete[] m_pReceive_Packet_Buffer;
	m_pReceive_Packet_Buffer = nullptr;
}

void Rigi_TCPSession::OnEvent_Receive(	__in char *_pData,
										__in size_t _nData_len )
{
	// 재정의하여 데이터 처리 부분을 추가한다.
	// if( nullptr != m_Func_Event_Receive )
	// 	m_Func_Event_Receive( _pData, _nData_len );
	std::cout << "[Rigi_TCPSession::OnEvent_Receive] << " << _pData << std::endl;
}

void Rigi_TCPSession::Handler_Receive( 	__in const boost::system::error_code& _error, 
										__in size_t _bytes_transferred)
{
	if (nullptr == m_pSocket)
	{
		//ASSERT(0 && "[Rigi_TCPSession::Handler_Receive] m_pSocket is not nullptr!!!");
		return;
	}

	if (_error)
	{	
		//std::cout << "[Rigi_TCPSession::Handler_Receive] OnClose !!" << std::endl; 
		OnEvent_Close();
		Close(_error);
	}
	else
	{
		//std::cout << "RECV << " << m_pReceive_Packet_Buffer << std::endl;
		OnEvent_Receive( m_pReceive_Packet_Buffer, _bytes_transferred );
		BufferClear();

		Async_Receive();
	}
}

void Rigi_TCPSession::OnEvent_Sended (__in size_t _bytes_transferre )
{
	// 재정의하여 데이터 처리 부분을 추가한다.
	// if( nullptr != m_Func_Event_Send)
	// 	m_Func_Event_Send( _bytes_transferre );
}

void Rigi_TCPSession::Handler_Send( __in const boost::system::error_code& _error, 
									__in size_t _bytes_transferred)
{
	if (_error)
	{	
		OnEvent_Close();

		std::cout << "[Rigi_TCPSession::Handler_Send] OnClose !!" << std::endl; 

		Close(_error);
	}
	else
	{
		OnEvent_Sended( _bytes_transferred );
	}
}

void Rigi_TCPSession::Async_Receive()
{
	if (nullptr == m_pSocket)
	{
		//ASSERT(0 && "[Rigi_TCPSession::Async_Receive] m_pSocket is not nullptr!!!");
		// todo 에러값 저장 
		return;
	}

	try
	{
		m_pSocket->async_read_some( boost::asio::buffer(m_pReceive_Packet_Buffer, m_nReceive_Packet_Size),
									boost::bind( &Rigi_TCPSession::Handler_Receive,
												this, 
												boost::asio::placeholders::error,
												boost::asio::placeholders::bytes_transferred) );
	}
	CATCH_EXCEPTION( );
}

size_t Rigi_TCPSession::Sync_Send(	__in const char* _pData, 
									__in size_t _nSize )
{
	if (nullptr == m_pSocket)
	{
		//ASSERT(0 && "[Rigi_TCPSession::Send] m_pSocket is not nullptr!!!");
		return -1;
	}

	try
	{
		size_t SendLeng = m_pSocket->send(boost::asio::buffer(_pData, _nSize));
		return SendLeng;
	}
	CATCH_EXCEPTION();

	Close();

	return -1;
}

void Rigi_TCPSession::ASync_Send( 	__in const char* _pData, 
							 		__in size_t _nSize)
{
	if (nullptr == m_pSocket)
	{
		//ASSERT(0 && "[Rigi_TCPSession::Async_Send] m_pSocket is not nullptr!!!");
		return;
	}

	try
	{
		boost::asio::async_write( 	*m_pSocket, 
									boost::asio::buffer(_pData, _nSize),
									boost::bind( &Rigi_TCPSession::Handler_Send, 
												this,
												boost::asio::placeholders::error,
												boost::asio::placeholders::bytes_transferred )
		);
	}
	CATCH_EXCEPTION( );
}

void Rigi_TCPSession::Close( __in const boost::system::error_code& _error )
{
	if (nullptr == m_pSocket)
		return;

	try
	{
		if (true == m_pSocket->is_open())
		{
			//std::string strClientIP = Get_SessionIP();
			//LOG(INFO) << "[CLOSE] " << strClientIP;

			m_pSocket->close();
			m_pSocket = nullptr;
		}
	}
	CATCH_EXCEPTION( );

	if(nullptr != m_pSessionPool)
		m_pSessionPool->Close_Session(this);
	m_pSessionPool = nullptr;
}

void Rigi_TCPSession::Close()
{
	boost::system::error_code error;
	Close(error);
}

const char *Rigi_TCPSession::Get_SessionIP()
{
	try
	{
		if( true == m_strIP_Client.empty())
		{
			if(nullptr != m_pSocket)
			{
				boost::asio::ip::tcp::endpoint remote_ep = m_pSocket->remote_endpoint();
				boost::asio::ip::address remote_ad = remote_ep.address();
				m_strIP_Client = remote_ad.to_string();

				return m_strIP_Client.c_str();
			}
			else
				return "0.0.0.0";
		}
		else
			return m_strIP_Client.c_str();
	}
	CATCH_EXCEPTION( );

	return "0.0.0.0";
}

void Rigi_TCPSession::SetSessionPool( __in Rigi_SessionPool *_pSessionPool )
{
	m_pSessionPool = _pSessionPool;
}

const Rigi_SessionPool *Rigi_TCPSession::GetSessionPool()
{
	return m_pSessionPool;
}

void Rigi_TCPSession::Set_TCPMgr( __in void * _pMgr )
{
	m_pTCPMgr = _pMgr;
}

void * Rigi_TCPSession::Get_TCPMgr()
{
	return m_pTCPMgr;
}

void Rigi_TCPSession::Make_Receive_Packet_Size( __in int _nPacket_Buffer_Size )
{
	if(1 < _nPacket_Buffer_Size )
	{
		m_nReceive_Packet_Size = _nPacket_Buffer_Size;
		m_pReceive_Packet_Buffer = new char[_nPacket_Buffer_Size];

		std::cout << "[Rigi_TCPSession::Make_Receive_Packet_Size][SUCC] buffer size => " << _nPacket_Buffer_Size << std::endl;
	}
	else
		std::cout << "[Rigi_TCPSession::Make_Receive_Packet_Size][FAIL] buffer size < 2 (buffer size = " << _nPacket_Buffer_Size << ")" << std::endl;
}

bool Rigi_TCPSession::SetTimeOut_Sync_Send( __in int _nMilieSecond )
{
	if(nullptr == m_pSocket)
		return false;

	boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO> option = boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>(_nMilieSecond); 
	m_pSocket->set_option(option);

	return true;
}

void Rigi_TCPSession::Add_Event_Handler_Close( __in Event_Handler_Close &&_Event )
{
	m_Func_Event_Close = std::move(_Event);
}

void Rigi_TCPSession::Add_Event_Handler_Init( __in Event_Handler_Init &&_Event )
{
	m_Func_Event_Init = std::move(_Event);
}

void Rigi_TCPSession::Add_Event_Handler_Receive( __in Event_Handler_Receive &&_Event )
{
	m_Func_Event_Receive = std::move(_Event);
}

void Rigi_TCPSession::Add_Event_Handler_Send( __in Event_Handler_Send &&_Event )
{
	m_Func_Event_Send = std::move(_Event);
}