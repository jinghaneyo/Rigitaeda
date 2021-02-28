#ifndef _RIGI_TCP_SERVER_H_
#define _RIGI_TCP_SERVER_H_

#include <vector>
#include <map>

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
        Rigi_TCPServerMgr() : m_acceptor(m_io_service)
        {
            m_nPort = 3333;
            m_pParents = nullptr;
        }

        virtual ~Rigi_TCPServerMgr()
        {
            Stop();
        }

    private:
        friend class Rigi_Server;

        int m_nPort;

        boost::asio::io_service         m_io_service;
        boost::asio::ip::tcp::acceptor  m_acceptor;

        Rigi_SessionPool                m_SessionPool;
        int                             m_nReceive_Packet_Size;

        Rigi_Server                     *m_pParents;

        void AsyncAccept()
        {
            SOCKET_TCP * pSocket = new SOCKET_TCP(m_io_service);

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
                    if( false == m_SessionPool.Add_Session(pSession, _pSocket) )
                    {
                        delete pSession;
                    }
                    else
                        OnEvent_Accept_Session( pSession );

                    AsyncAccept();
                }
                catch(const std::exception& e)
                {
                    std::cerr << "[Exception][Handle_accept] >> " << e.what() << '\n';
                }
            }
            else
            {
                //AfxGetPtr::SetLastError(error.message());
            }
        }

        // 버퍼 크기는 new TCPSession 을 할때 해야 함으로 public 으로 아무때나 호출 할수 없게 private 으로 한다
        void Set_Receive_Packet_Size( __in int _nReceive_Packet_Size )
        {
            m_nReceive_Packet_Size = _nReceive_Packet_Size;
        }
    public:
        // -----------------------------------------------------------------------------------
        // 재정의하여 데이터 처리 부분을 추가한다.
        virtual void OnEvent_Accept_Session( __in TCP_TMPL *_pSession ) { };

        // 재정의하여 데이터 처리 부분을 추가한다.
        virtual bool OnEvent_Init() { return true; };
        // -----------------------------------------------------------------------------------

        int Get_Port() { return m_nPort; };

        bool Run(   __in int _nPort, 
                    __in int _nMaxClient,
                    __in Rigi_Server *_pParents )
        {
            if(1 > _nPort)
            {
                ASSERT(0 && "[Rigi_TCPServerMgr::Run] port < 0 !!");
                return false;
            }
            if(1 > _nMaxClient)
            {
                ASSERT(0 && "[Rigi_TCPServerMgr::Run] _nMaxClient < 0 !!");
                return false;
            }
            if( nullptr == _pParents)
            {
                ASSERT(0 && "[Rigi_TCPServerMgr::Run] _nMaxClient < 0 !!");
                return false;
            }

            m_nPort = _nPort;
            m_pParents = _pParents;

            m_SessionPool.Set_MaxClient(_nMaxClient);
            m_SessionPool.Set_Rigi_Server( m_pParents );
            m_SessionPool.Set_Receive_Packet_Size(m_nReceive_Packet_Size);

            m_io_service.reset();

            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), m_nPort);

            // Init 이벤트 호출
            if( false == OnEvent_Init() )
            {
                ASSERT(0 && "[Rigi_TCPMgr::Run] OnEvent_Init is false !!");
                return false;
            }

            m_acceptor.open(endpoint.protocol());

            boost::asio::socket_base::reuse_address option(true);
            m_acceptor.set_option(option);

            m_acceptor.bind(endpoint);
            m_acceptor.listen(_nMaxClient);

            std::cout << "[Rigi_TCPServerMgr] Accept Start >> " << std::endl;
            AsyncAccept();

            std::cout << "[Rigi_TCPServerMgr] Server Start >> " << std::endl;
            boost::system::error_code ec;
            m_io_service.run(ec);

            return true;
        }

        void Stop()
        {
            m_acceptor.cancel();
            m_acceptor.close();

            m_io_service.stop();

            m_SessionPool.Clear();
        }

        std::string Get_LocalServerIP()
        {
            boost::asio::ip::tcp::resolver resolver(m_io_service);
            boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
            boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve( query );
            boost::asio::ip::tcp::endpoint ep = *iter;

            std::string strLocalIP = ep.address().to_string();

            return strLocalIP;
        }
    };
}

#endif