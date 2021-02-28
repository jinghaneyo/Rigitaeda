/*
Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2013-2019 lee,gil-jae (jinghaneyo@gmail..com)
*/

#ifndef _RIGI_SERVER_H_
#define _RIGI_SERVER_H_

#include "Rigi_Def.hpp"
#include "Rigi_TCPServerMgr.hpp"
#include "Rigi_TCPSession.hpp"
#include "Rigi_EventHandler.hpp"

namespace Rigitaeda
{
    class Rigi_UDPServer;

    class Rigi_Server
    {
    public:
        Rigi_Server( __in int _nReceive_Packet_Size = 10240 );
        virtual ~Rigi_Server();
    private:
        friend class Rigi_SessionPool;

        int m_nReceive_Packet_Size;
        Rigi_TCPServerMgr<Rigi_TCPSession> *m_pTCP_Mgr;
        Rigi_UDPServer *m_pUDP_Server;

        Event_Handler_Close		m_Func_Event_Close;
		Event_Handler_Init 		m_Func_Event_Init;
		Event_Handler_Receive 	m_Func_Event_Receive;
		Event_Handler_Send		m_Func_Event_Send;
    public:
        bool Run(   __in int _nPort, 
                    __in int _nMaxClient )
        {
            // 기본 클래스로 인스턴스 생성하며, EventHandler로 콜백함수로 Receive를 받도록 한다
            m_pTCP_Mgr = new Rigi_TCPServerMgr<Rigi_TCPSession>();
            m_pTCP_Mgr->Set_Receive_Packet_Size(m_nReceive_Packet_Size);
            return m_pTCP_Mgr->Run( _nPort, _nMaxClient, this );
        }

        template <typename TCP_TMPL>
        bool Run(   __in int _nPort, 
                    __in int _nMaxClient,
                    __in Rigi_TCPServerMgr<TCP_TMPL> *_pMgr )
        {
            if(nullptr == _pMgr)
            {
                ASSERT(0 && "Rigi_TCPMgr is not nullptr!!!");
                return false;
            }

            _pMgr->Set_Receive_Packet_Size(m_nReceive_Packet_Size);
            return _pMgr->Run( _nPort, _nMaxClient, this );
        }

        void Stop();

        void Add_Event_Handler_Close( __in Event_Handler_Close &&_Event )       { m_Func_Event_Close    = std::move(_Event); }
        void Add_Event_Handler_Init( __in Event_Handler_Init &&_Event )         { m_Func_Event_Init     = std::move(_Event); }
        void Add_Event_Handler_Receive( __in Event_Handler_Receive &&_Event )   { m_Func_Event_Receive  = std::move(_Event); }
        void Add_Event_Handler_Send( __in Event_Handler_Send &&_Event )         { m_Func_Event_Send     = std::move(_Event); }

        Event_Handler_Close     Get_Event_Handler_Close()      { return m_Func_Event_Close;    }
        Event_Handler_Init      Get_Event_Handler_Init()       { return m_Func_Event_Init;     }
        Event_Handler_Receive   Get_Event_Handler_Receive()    { return m_Func_Event_Receive;  }
        Event_Handler_Send      Get_Event_Handler_Send()       { return m_Func_Event_Send;     }
    };
}

#endif