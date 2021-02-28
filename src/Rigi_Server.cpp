#include "Rigi_Server.hpp"
#include "Rigi_TCPServerMgr.hpp"

using namespace Rigitaeda;

Rigi_Server::Rigi_Server( __in int _nReceive_Packet_Size )
{
    m_pTCP_Mgr = nullptr;
    m_pUDP_Server = nullptr;
    m_nReceive_Packet_Size = _nReceive_Packet_Size;
}

Rigi_Server::~Rigi_Server()
{
    Stop();
}

void Rigi_Server::Stop()
{
    if(nullptr != m_pTCP_Mgr)
    {
        m_pTCP_Mgr->Stop();

        delete m_pTCP_Mgr;
        m_pTCP_Mgr = nullptr;
    }

    // if(nullptr != m_pServerUDP)
    // {
    //     //m_pServerUDP->Stop();

    //     delete m_pServerUDP;
    //     m_pServerUDP = nullptr;
    // }
}