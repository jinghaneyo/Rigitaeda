#ifdef _BIN
#include <string>
#include <glog/logging.h>
#include "Rigi_Server.hpp"

using namespace Rigitaeda;

class TCPSessionTest : public Rigi_TCPSession
{
public:
	TCPSessionTest() { }
	virtual ~TCPSessionTest() { }

	void OnEvent_Receive(	__in char *_pData,
							__in size_t _nData_len )
	{
		std::cout << "[TCPSessionTest::OnReceive] >> " << _pData << std::endl;
	}

	void OnEvent_Close()
	{
		//std::cout << "[TCPSessionTest::OnClose] >> " << Get_SessionIP() << std::endl;
		std::cout << "[TCPSessionTest::OnClose] >> " << std::endl;
	}
};

int main()
{
	// google::InitGoogleLogging("DUMP");                // 구글 로그 초기화
 	// google::SetLogDestination( google::GLOG_INFO, "./DUMP." );      // 출력 로그 레벨 설정, 출력 위치 Test. <= 파일 저장시 접두사로 사용됨
	//google::EnableLogCleaner(3);

    std::cout << "[START] << server run" << std::endl;

	Rigi_TCPMgr<TCPSessionTest> tcp;
	Rigi_Server server;
	server.Run( 3333, 100, &tcp );

	return 1;
}
#endif
