#ifndef _RIGI_EVENT_HANDLER_H
#define _RIGI_EVENT_HANDLER_H

#include <vector>
#include <functional>
#include "Rigi_Def.hpp"

namespace Rigitaeda
{
	class Rigi_EventHandler
	{
	public:
		Rigi_EventHandler();
		virtual ~Rigi_EventHandler();
	//private:
		// std::vector<Event_PreStart> m_vecEvent_PreStart;
		// std::vector<Event_Started> m_vecEvent_Started;
		// std::vector<Event_PreStop> m_vecEvent_PreStop;
		// std::vector<Event_Stoped> m_vecEvent_Stoped;
		// std::vector<Event_Connected_Session> m_vecEvent_Connected;
		// std::vector<Event_Received> m_vecEvent_Received;
		// std::vector<Event_PreClose_Session> m_vecEvent_PreClose;
		// std::vector<Event_Closed_Session> m_vecEvent_Closed;

	public:
		// void AddEvent_PreStart(Event_PreStart Event);
		// void AddEvent_Started(Event_Started Event);
		// void AddEvent_PreStop(Event_PreStop Event);
		// void AddEvent_Stoped(Event_Stoped Event);
		// void AddEvent_Connected_Session(Event_Connected_Session Event);
		// void AddEvent_Received(Event_Received Event);
		// void AddEvent_Closed_Session(Event_Closed_Session Event);
		void Event_Clear();
	};
};

#endif