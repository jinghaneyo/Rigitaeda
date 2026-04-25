#include "Rigi_TCPSession.hpp"
#include "Rigi_TCPServerMgr.hpp"
#include "Rigi_SessionPool.hpp"
#include <vector>
#include <memory>

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
	m_bSending = false;

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
		OnEvent_Close();
		Close(_error);
	}
	else
	{
		if (m_pCodec)
		{
			// 코덱이 설정된 경우: 스트림을 완성 메시지 단위로 분리 후 OnMessage 호출
			m_pCodec->decode(m_pReceive_Packet_Buffer, _bytes_transferred,
				[this](const char* msg, size_t len) {
					if (m_dispatcher)
					{
						// 워커 스레드 풀로 디스패치: shared_ptr로 버퍼 수명 보장
						auto buf = std::make_shared<std::vector<char>>(msg, msg + len);
						m_dispatcher([this, buf]() {
							OnMessage(buf->data(), buf->size());
						});
					}
					else
					{
						OnMessage(msg, len);
					}
				});
		}
		else
		{
			// 코덱 미설정: 기존 방식 그대로 (하위 호환)
			OnEvent_Receive(m_pReceive_Packet_Buffer, _bytes_transferred);
		}
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

int Rigi_TCPSession::Sync_Send(	__in const char* _pData,
								__in size_t _nSize )
{
	if (nullptr == m_pSocket)
	{
		//ASSERT(0 && "[Rigi_TCPSession::Send] m_pSocket is not nullptr!!!");
		return -1;
	}

	try
	{
		int SendLeng = static_cast<int>(m_pSocket->send(boost::asio::buffer(_pData, _nSize)));
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
		return;

	// 코덱이 있으면 encode(프레임 추가), 없으면 그대로
	std::vector<char> frame;
	if (m_pCodec)
		frame = m_pCodec->encode(_pData, _nSize);
	else
		frame = std::vector<char>(_pData, _pData + _nSize);

	std::lock_guard<std::mutex> lock(m_send_mutex);
	bool idle = !m_bSending && m_send_queue.empty();
	m_send_queue.push_back(std::move(frame));
	if (idle)
		do_send();
}

void Rigi_TCPSession::do_send()
{
	// m_send_mutex 보유 상태에서 호출
	if (m_send_queue.empty() || nullptr == m_pSocket)
	{
		m_bSending = false;
		return;
	}

	m_bSending = true;
	auto buf = std::make_shared<std::vector<char>>(std::move(m_send_queue.front()));
	m_send_queue.pop_front();

	boost::asio::async_write(*m_pSocket,
		boost::asio::buffer(*buf),
		[this, buf](const boost::system::error_code& error, size_t bytes_transferred)
		{
			if (!error)
			{
				OnEvent_Sended(bytes_transferred);
				std::lock_guard<std::mutex> lock(m_send_mutex);
				do_send();  // 다음 항목 전송
			}
			else
			{
				{
					std::lock_guard<std::mutex> lock(m_send_mutex);
					m_bSending = false;
					m_send_queue.clear();
				}
				Handler_Send(error, bytes_transferred);
			}
		});
}

void Rigi_TCPSession::Close( __in const boost::system::error_code& _error )
{
	if (nullptr == m_pSocket)
		return;

	try
	{
		if (m_pSocket->is_open())
			m_pSocket->close();
	}
	CATCH_EXCEPTION( );

	delete m_pSocket;
	m_pSocket = nullptr;

	{
		std::lock_guard<std::mutex> lock(m_send_mutex);
		m_send_queue.clear();
		m_bSending = false;
	}

	// pool을 먼저 null로 만들어야 Close_Session → delete this 이후 멤버 접근 UB를 막는다
	Rigi_SessionPool* pool = m_pSessionPool;
	m_pSessionPool = nullptr;
	if (nullptr != pool)
		pool->Close_Session(this);  // 내부에서 delete this 가능 — 이후 멤버 접근 없음
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