#ifndef _RIGI_TCP_SERVER_H_
#define _RIGI_TCP_SERVER_H_

#include <vector>
#include <map>
#include <memory>

#include "Rigi_Def.hpp"
#include "Rigi_TCPSession.hpp"
#include "Rigi_SessionPool.hpp"

namespace Rigitaeda
{
    class Rigi_Server;

    template <typename TCP_TMPL>
    class Rigi_TCPServerMgr
    {
    public:
        Rigi_TCPServerMgr() : m_acceptor(m_io_context)
        {
            m_nPort = 3333;
            m_pParents = nullptr;
            m_nReceive_Packet_Size = 0;
            m_nThreadPoolSize = 0;
        }

        virtual ~Rigi_TCPServerMgr()
        {
            Stop();
        }

    private:
        friend class Rigi_Server;

        int m_nPort;

        boost::asio::io_context         m_io_context;
        boost::asio::ip::tcp::acceptor  m_acceptor;

        Rigi_SessionPool                m_SessionPool;
        int                             m_nReceive_Packet_Size;

        Rigi_Server                     *m_pParents;

        // 워커 스레드 풀: OnMessage를 io_context 외부에서 처리
        unsigned int                               m_nThreadPoolSize;
        std::unique_ptr<boost::asio::thread_pool>  m_thread_pool;

        void AsyncAccept()
        {
            SOCKET_TCP * pSocket = new SOCKET_TCP(m_io_context);

            m_acceptor.async_accept(    *pSocket,
                                        boost::bind(&Rigi_TCPServerMgr::Handle_accept,
                                                    this,
                                                    pSocket,
                                                    boost::asio::placeholders::error)
            );
        }

        void Handle_accept( __in SOCKET_TCP *_pSocket,
                            __in const boost::system::error_code& _error )
        {
            if (!_error)
            {
                try
                {
                    TCP_TMPL *pSession = new TCP_TMPL();
                    pSession->Set_TCPMgr((void *)this);

                    // 스레드 풀이 설정된 경우 OnMessage를 워커로 디스패치
                    if (m_thread_pool)
                    {
                        boost::asio::thread_pool* pool = m_thread_pool.get();
                        pSession->Set_Dispatcher([pool](std::function<void()> task) {
                            boost::asio::post(*pool, std::move(task));
                        });
                    }

                    if( false == m_SessionPool.Add_Session(pSession, _pSocket) )
                    {
                        delete pSession;
                        delete _pSocket;  // Add_Session 실패 시 소켓 누수 방지
                    }
                    else
                        OnEvent_Accept_Session( pSession );
                }
                catch(const std::exception& e)
                {
                    std::cerr << "[Exception][Handle_accept] >> " << e.what() << '\n';
                    delete _pSocket;  // 예외 발생 시에도 소켓 해제
                }

                // try 블록 밖으로 이동: 예외 발생 시에도 Accept를 계속함
                AsyncAccept();
            }
            else
            {
                delete _pSocket;  // 에러 시 소켓 해제
            }
        }

        void Set_Receive_Packet_Size( __in int _nReceive_Packet_Size )
        {
            m_nReceive_Packet_Size = _nReceive_Packet_Size;
        }
    public:
        virtual void OnEvent_Accept_Session( __in TCP_TMPL *_pSession ) { };
        virtual bool OnEvent_Init() { return true; };

        // OnMessage 처리용 워커 스레드 수 설정 (Run() 호출 전에 설정해야 함)
        // thread_count == 0 이면 io_context 스레드에서 직접 호출 (기본값)
        void Set_ThreadPool( unsigned int thread_count )
        {
            m_nThreadPoolSize = thread_count;
        }

        int Get_Port() { return m_nPort; };

        bool Run(   __in int _nPort,
                    __in int _nMaxClient,
                    __in Rigi_Server *_pParents )
        {
            if(1 > _nPort)
            {
                ASSERT(0 && "[Rigi_TCPServerMgr::Run] port < 1 !!");
                return false;
            }
            if(1 > _nMaxClient)
            {
                ASSERT(0 && "[Rigi_TCPServerMgr::Run] _nMaxClient < 1 !!");
                return false;
            }
            if( nullptr == _pParents)
            {
                ASSERT(0 && "[Rigi_TCPServerMgr::Run] _pParents is nullptr !!");
                return false;
            }

            m_nPort = _nPort;
            m_pParents = _pParents;

            m_SessionPool.Set_MaxClient(_nMaxClient);
            m_SessionPool.Set_Rigi_Server( m_pParents );
            m_SessionPool.Set_Receive_Packet_Size(m_nReceive_Packet_Size);

            m_io_context.restart();

            // 스레드 풀 생성 (Set_ThreadPool 호출 시에만)
            if (m_nThreadPoolSize > 0)
                m_thread_pool = std::make_unique<boost::asio::thread_pool>(m_nThreadPoolSize);

            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), m_nPort);

            if( false == OnEvent_Init() )
            {
                ASSERT(0 && "[Rigi_TCPMgr::Run] OnEvent_Init is false !!");
                return false;
            }

            m_acceptor.open(endpoint.protocol());

            boost::asio::socket_base::reuse_address option(true);
            m_acceptor.set_option(option);

            m_acceptor.bind(endpoint);
            m_acceptor.listen(boost::asio::socket_base::max_listen_connections);

            std::cout << "[Rigi_TCPServerMgr] Accept Start >> " << std::endl;
            AsyncAccept();

            std::cout << "[Rigi_TCPServerMgr] Server Start >> " << std::endl;
            boost::system::error_code ec;
            m_io_context.run(ec);

            return true;
        }

        void Stop()
        {
            m_acceptor.cancel();
            m_acceptor.close();

            m_io_context.stop();

            m_SessionPool.Clear();

            // 스레드 풀이 있으면 보류 중인 OnMessage 작업 모두 완료 후 종료
            if (m_thread_pool)
            {
                m_thread_pool->join();
                m_thread_pool.reset();
            }
        }

        std::string Get_LocalServerIP()
        {
            boost::asio::ip::tcp::resolver resolver(m_io_context);
            boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
            boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve( query );
            boost::asio::ip::tcp::endpoint ep = *iter;

            std::string strLocalIP = ep.address().to_string();

            return strLocalIP;
        }
    };
}

#endif
