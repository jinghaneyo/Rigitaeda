#ifndef _RIGI_TCP_SESSION_H_
#define _RIGI_TCP_SESSION_H_

#include <deque>
#include <memory>
#include <mutex>
#include "Rigi_Def.hpp"
#include "Rigi_Codec.hpp"

namespace Rigitaeda
{
	class Rigi_SessionPool;

	typedef std::function<void(void)> 						Event_Handler_Close;
	typedef std::function<bool(void)> 						Event_Handler_Init;
	typedef std::function<void( __in char *, __in size_t )> Event_Handler_Receive;
	typedef std::function<void( __in size_t )> 				Event_Handler_Send;

	class Rigi_TCPSession
	{
	public:
		Rigi_TCPSession();
		virtual ~Rigi_TCPSession();

	private:
		friend class Rigi_SessionPool;

		int   m_nReceive_Packet_Size;
		char *m_pReceive_Packet_Buffer;

		SOCKET_TCP       *m_pSocket;
		Rigi_SessionPool *m_pSessionPool;
		std::string       m_strIP_Client;
		void             *m_pTCPMgr;

		// 코덱 플러그인 (nullptr이면 OnEvent_Receive 직접 호출 — 하위 호환)
		std::unique_ptr<Rigi_Codec> m_pCodec;

		// 내부 송신 큐: ASync_Send의 동시 호출 안전 보장
		std::deque<std::vector<char>> m_send_queue;
		std::mutex                    m_send_mutex;
		bool                          m_bSending;

		// OnMessage 디스패처: nullptr이면 io_context 스레드에서 직접 호출
		std::function<void(std::function<void()>)> m_dispatcher;

		Event_Handler_Close		m_Func_Event_Close;
		Event_Handler_Init 		m_Func_Event_Init;
		Event_Handler_Receive 	m_Func_Event_Receive;
		Event_Handler_Send		m_Func_Event_Send;

		void Handler_Receive( 	__in const boost::system::error_code& _error,
								__in size_t _bytes_transferred);

		void Handler_Send( 	__in const boost::system::error_code& _error,
							__in size_t _bytes_transferred);

		void Close( __in const boost::system::error_code& _error );

		// 송신 큐에서 다음 항목을 꺼내 async_write 시작 (m_send_mutex 보유 상태에서 호출)
		void do_send();

		inline void BufferClear()
		{
			memset(m_pReceive_Packet_Buffer, 0, m_nReceive_Packet_Size);
		}

	public:
		// -----------------------------------------------------------
		// Event — 코덱 미설정 시 사용 (하위 호환)
		virtual void OnEvent_Receive(	__in char *_pData, __in size_t _nData_len );
		virtual void OnEvent_Sended (	__in size_t _bytes_transferre );

		virtual void OnEvent_Close() {}
		virtual bool OnEvent_Init()  { return true; }

		// 코덱 설정 시 사용: 완성된 메시지 단위로 호출됨
		virtual void OnMessage( __in const char *_pData, __in size_t _nData_len ) {}
		// -----------------------------------------------------------

		// 코덱 플러그인 설정 — 레고 블럭처럼 조합
		// 예) Set_Codec(make_unique<Rigi_Codec_Delimiter>(vector<char>{'\x17','\x17'}))
		//     Set_Codec(make_unique<Rigi_Codec_LengthPrefix>(4))
		void Set_Codec( std::unique_ptr<Rigi_Codec> _pCodec )
		{
			m_pCodec = std::move(_pCodec);
		}

		// OnMessage를 워커 스레드로 디스패치할 함수 설정
		// 예) session->Set_Dispatcher([&pool](auto task){ boost::asio::post(pool, std::move(task)); });
		// 설정하지 않으면 io_context 스레드에서 직접 호출 (하위 호환)
		void Set_Dispatcher( std::function<void(std::function<void()>)> _fn )
		{
			m_dispatcher = std::move(_fn);
		}

		void Make_Receive_Packet_Size( __in int _nPacket_Buffer_Size );

		void Async_Receive();

		int  Sync_Send ( __in const char* _pData, __in size_t _nSize);
		void ASync_Send( __in const char* _pData, __in size_t _nSize);

		SOCKET_TCP * GetSocket() { return m_pSocket; }
		void SetSocket( __in SOCKET_TCP *_pSocket ) { m_pSocket = _pSocket; }

		const char *Get_SessionIP();
		const char *GetPacket_Receive() { return m_pReceive_Packet_Buffer; }

		void SetSessionPool( __in Rigi_SessionPool *_pSessionPool );
		const Rigi_SessionPool * GetSessionPool();

		void Set_TCPMgr( __in void * _pMgr );
		virtual void * Get_TCPMgr();

		void Close();

		bool SetTimeOut_Sync_Send( __in int _nMilieSecond );

		void Add_Event_Handler_Close( __in Event_Handler_Close &&_Event );
		void Add_Event_Handler_Init( __in Event_Handler_Init &&_Event );
		void Add_Event_Handler_Receive( __in Event_Handler_Receive &&_Event );
		void Add_Event_Handler_Send( __in Event_Handler_Send &&_Event );
	};
}

#endif
