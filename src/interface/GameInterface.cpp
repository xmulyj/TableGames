/*
 * GameInterface.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameInterface.h"
#include "ConfigReader.h"
#include "Socket.h"

#include "../KeyDefine.h"

#include <assert.h>
#include <time.h>
#include <arpa/inet.h>

static ConfigReader gConfigReader;

IMPL_LOGGER(GameInterface, logger);

bool GameInterface::Start()
{
	//Add Your Code Here
	if(!gConfigReader.Init("./config/server.conf"))
		assert(0);
	string ip = gConfigReader.GetValue("ServerIP", "");
	assert(ip.size() > 0);
	int port = gConfigReader.GetValue("ServerPort", -1);
	assert(port > 0);

	//监听端口
	int listen_fd = Listen(port, ip.c_str());
	if(listen_fd < 0)
	{
		LOG_ERROR(logger, "listen failed on: "<<ip<<":"<<port);
		assert(0);
	}
	LOG_INFO(logger, "listen succ on: "<<ip<<":"<<port);

	//添加时钟:用于定时检查room是否存在
	IEventServer *event_server = GetEventServer();
	assert(event_server != NULL);
	if(event_server->AddTimer(this, 1000, true) == false)
	{
		LOG_ERROR(logger, "add timer failed!");
		assert(0);
	}
	LOG_INFO(logger, "add master timer succ. timeout_ms=1000");

	//进入循环
	LOG_INFO(logger, "GameInterface goto run_loop...");
	event_server->RunLoop();

	return true;
}

int32_t GameInterface::GetSocketRecvTimeout()
{
	return -1;
}

int32_t GameInterface::GetSocketIdleTimeout()
{
	return 3000;
}

int32_t GameInterface::GetMaxConnections()
{
	return 1000;
}

bool GameInterface::OnReceiveProtocol(int32_t fd, ProtocolContext *context, bool &detach_context)
{
	//Add Your Code Here
	LOG_DEBUG(logger, "receive protocol on fd="<<fd);
	
	KVData *kvdata = (KVData*)context->protocol;

	int protocol;
	if(!kvdata->GetValue(KEY_Protocol, protocol))
	{
		LOG_ERROR(logger, "get protocol type failed. fd="<<fd);
		return false;
	}

	switch(protocol)
	{
	case ReportRoomInfo:
		return OnReportRoomInfo(fd, kvdata);
		break;
	case GetAllRoom:
		return OnGetAllRoom(fd, kvdata);
		break;
	case GetRoomAddr:
		return OnGetRoomAddr(fd, kvdata);
		break;
	default:
		LOG_ERROR(logger, "unknow protocol type="<<protocol<<". fd="<<fd);
		return false;
		break;
	}
	return true;
}

void GameInterface::OnSendSucc(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_DEBUG(logger, "send protocol succ on fd="<<fd<<", info='"<<context->Info<<"'");
	DeleteProtocolContext(context);
	return ;
}

void GameInterface::OnSendError(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_ERROR(logger, "send protocol failed on fd="<<fd<<", info='"<<context->Info<<"'");
	DeleteProtocolContext(context);
	return ;
}

void GameInterface::OnSendTimeout(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_WARN(logger, "send protocol timeout on fd="<<fd<<", info='"<<context->Info<<"'");
	DeleteProtocolContext(context);
	return ;
}

void GameInterface::OnSocketFinished(int32_t fd)
{
	//Add Your Code Here
	LOG_INFO(logger, "socket finished. fd="<<fd);
	
	//close it?
	Socket::Close(fd);

	return ;
}

void GameInterface::OnTimeout(uint64_t nowtime_ms)
{
	//检查超时的任务
	LOG_DEBUG(logger, "timer timeout:");
	CheckRoomStatus();
}

void GameInterface::CheckRoomStatus()
{
	LOG_DEBUG(logger, "timer timeout: check room status...");
	uint32_t now = (uint32_t)time(NULL);
	RoomInfoMap::iterator it;
	for(it=m_RoomInfoMap.begin(); it!=m_RoomInfoMap.end(); )
	{
		if(it->second.TimeStamp+5 >= now)
		{
			++it;
			continue;
		}

		LOG_ERROR(logger, "CheckRoomStatus: room timeout and removed. RoomID="<<it->second.RoomID);
		m_RoomInfoMap.erase(it++);
	}
}

#define SerializeKVData(kvdata, send_context, info)  do{  \
send_context = NewProtocolContext();  \
assert(send_context != NULL);  \
send_context->type = DTYPE_BIN;  \
send_context->Info = info;  \
uint32_t header_size = m_ProtocolFactory->HeaderSize();  \
uint32_t body_size = kvdata.Size();  \
send_context->CheckSize(header_size+body_size);  \
m_ProtocolFactory->EncodeHeader(send_context->Buffer, body_size);  \
kvdata.Serialize(send_context->Buffer+header_size);  \
send_context->Size = header_size+body_size;  \
}while(0)

bool GameInterface::OnReportRoomInfo(int fd, KVData *kvdata)
{
	RoomInfo room_info;

	uint32_t ArrayCount;
	char     *NumArray;

	if(!kvdata->GetValue(KEY_RoomID, room_info.RoomID))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_RoomIP, room_info.RoomIP))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get RoomIP failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_RoomPort, room_info.RoomPort))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get RoomPort failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientNum, room_info.ClientNum))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get ClientNum failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_TableNum, room_info.TableNum))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get TableNum failed. fd="<<fd);
		return false;
	}

	if(!kvdata->GetValue(KEY_NumArray, NumArray, ArrayCount))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get NumArray failed. fd="<<fd);
		return false;
	}
	ArrayCount /= sizeof(int);
	if(ArrayCount != room_info.TableNum)
	{
		LOG_ERROR(logger, "OnReportRoomInfo: ArrayCount Invalid. ArrayCount="<<ArrayCount
				<<",TableNum"<<room_info.TableNum
				<<", fd="<<fd);
		return false;
	}
	for(int i=0;i<room_info.TableNum; ++i)
	{
		NumArray[i] = ntohl(NumArray[i]);
		room_info.NumArray.push_back(NumArray[i]);
	}

	LOG_DEBUG(logger, "OnReportRoomInfo: fd="<<fd
			<<",RoomID="<<room_info.RoomID
			<<",RoomIP="<<room_info.RoomIP
			<<",RoomPort="<<room_info.RoomPort
			<<",ClientNum="<<room_info.ClientNum
			<<",TableNum="<<room_info.TableNum);

	room_info.TimeStamp = (uint32_t)time(NULL);
	RoomInfoMap::iterator it = m_RoomInfoMap.find(room_info.RoomID);
	if(it == m_RoomInfoMap.end())
	{
		std::pair<RoomInfoMap::iterator, bool> ret;
		ret = m_RoomInfoMap.insert(std::make_pair(room_info.RoomID, room_info));
		assert(ret.second == true);
		LOG_INFO(logger, "OnReportRoomInfo: Add New Room. RoomID="<<room_info.RoomID<<",fd="<<fd);
	}
	else
	{
		it->second = room_info;
		LOG_DEBUG(logger, "OnReportRoomInfo: Update RoomInfo. RoomID="<<room_info.RoomID<<",fd="<<fd);
	}

	return true;
}

bool GameInterface::OnGetAllRoom(int fd, KVData *kvdata)
{
	uint32_t ClientID;
	string   ClientName;
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnReportRoomInfo: get ClientName failed. fd="<<fd);
		return false;
	}
	LOG_DEBUG(logger, "OnGetAllRoom: ClientID="<<ClientID
			<<", ClientName="<<ClientName
			<<",fd="<<fd);

	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "GetAllRoomRsp";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)GetAllRoomRsp);
	send_kvdata.SetValue(KEY_RoomNum, (int)m_RoomInfoMap.size());

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();
	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	if(m_RoomInfoMap.size() > 0)
	{
		int buf_size = sizeof(int)*2*m_RoomInfoMap.size();
		send_context->CheckSize(KVData::SizeBytes(buf_size);

		char *data_buffer = send_context->Buffer+send_context->Size;
		KVBuffer kv_buffer = KVData::BeginWrite(data_buffer, KEY_NumArray, true);
		char *num_array = kv_buffer.second;

		RoomInfoMap::iterator it;
		for(it=m_RoomInfoMap.begin(); it!=m_RoomInfoMap.end(); ++it)
		{
			int value;
			value = htonl(it->second.RoomID);
			*num_array = value;
			num_array += sizeof(value);

			value = htonl(it->second.ClientNum);
			*num_array = value;
			num_array += sizeof(value);
		}
		assert(num_array-kv_buffer.second == buf_size);
		send_context->Size += KVData::EndWrite(kv_buffer, buf_size);
	}

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(SendProtocol(fd, send_context) == false)
	{
		LOG_ERROR(logger, "send GetAllRoomRsp to framework failed.fd="<<fd);
		DeleteProtocolContext(send_context);
		return false;
	}
	LOG_DEBUG(logger, "send GetAllRoomRsp to framework succ.fd="<<fd);
	return true;
}

bool GameInterface::OnGetRoomAddr(int fd, KVData *kvdata)
{
	int      RoomID;
	uint32_t ClientID;
	string   ClientName;
	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnGetRoomAddr: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnGetRoomAddr: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnGetRoomAddr: get ClientName failed. fd="<<fd);
		return false;
	}


	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)GetRoomAddrRsp);
	send_kvdata.SetValue(KEY_RoomID, RoomID);

	RoomInfoMap::iterator it = m_RoomInfoMap.find(RoomID);
	if(it != m_RoomInfoMap.end())
	{
		send_kvdata.SetValue(KEY_RoomIP, it->second.RoomIP);
		send_kvdata.SetValue(KEY_RoomPort, it->second.RoomPort);
		LOG_DEBUG(logger, "OnGetRoomAddr: RoomID="<<RoomID
					<<",ClientID="<<ClientID
					<<",ClientName="<<ClientName
					<<",RoomIP="<<it->second.RoomIP
					<<",RoomPort="<<it->second.RoomPort
					<<",fd="<<fd);
	}
	else
	{
		LOG_ERROR(logger, "OnGetRoomAddr: not find room. RoomID="<<RoomID
					<<",ClientID="<<ClientID
					<<",ClientName="<<ClientName
					<<",fd="<<fd);
	}

	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "GetRoomAddrRsp";

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();
	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(SendProtocol(fd, send_context) == false)
	{
		LOG_ERROR(logger, "send GetRoomAddrRsp to framework failed.fd="<<fd);
		DeleteProtocolContext(send_context);
		return false;
	}
	LOG_DEBUG(logger, "send GetRoomAddrRsp to framework succ.fd="<<fd);
	return true;
}
