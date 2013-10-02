/*
 * GameRoom.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameRoom.h"
#include <assert.h>

#include "../KeyDefine.h"

#include "ConfigReader.h"
static ConfigReader gConfigReader;

IMPL_LOGGER(GameRoom, logger);

bool GameRoom::Start()
{
	//Add Your Code Here
	if(!gConfigReader.Init("../config/server.conf"))
	{
		assert(0);
	}

	m_IP = gConfigReader.GetValue("ServerIP", "");
	assert(m_IP.size() > 0);
	m_Port = gConfigReader.GetValue("ServerPort", -1);
	assert(m_Port > 0);
	m_ID = gConfigReader.GetValue("RoomID", -1);
	assert(m_Port > 0);
	m_TableNum = gConfigReader.GetValue("TableNum", -1);
	assert(m_TableNum > 0);
	m_PackNum = gConfigReader.GetValue("PackNum", -1);
	assert(m_PackNum > 0);
	m_ClientNum = 0;

	for(int i=0; i<m_TableNum; ++i)
	{
		TractorTable table(m_PackNum);
		m_Tables.push_back(table);
	}

	//Interface
	m_InterfaceIP = gConfigReader.GetValue("InterfaceIP", "");
	assert(m_InterfaceIP.size() > 0);
	m_InterfacePort = gConfigReader.GetValue("InterfacePort", -1);
	assert(m_InterfacePort > 0);

	m_InterfaceFD = -1;

	IEventServer *event_server = GetEventServer();
	//注册定时发送ping包时钟
	if(event_server->AddTimer(this, 1000, true) == false)
	{
		LOG_ERROR(logger, "GameRoom add timer failed. RoomID="<<m_ID);
		assert(0);
	}

	event_server->RunLoop();
	return true;
}

int32_t GameRoom::GetSocketRecvTimeout()
{
	return -1;
}

int32_t GameRoom::GetSocketIdleTimeout()
{
	return 3000;
}

int32_t GameRoom::GetMaxConnections()
{
	return 1000;
}

bool GameRoom::OnReceiveProtocol(int32_t fd, ProtocolContext *context, bool &detach_context)
{
	//Add Your Code Here
	LOG_DEBUG(logger, "receive protocol on fd="<<fd);
	
	return true;
}

void GameRoom::OnSendSucc(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_DEBUG(logger, "send protocol succ on fd="<<fd<<", info='"<<context->Info<<"'");
	
	return ;
}

void GameRoom::OnSendError(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_ERROR(logger, "send protocol failed on fd="<<fd<<", info='"<<context->Info<<"'");
	
	return ;
}

void GameRoom::OnSendTimeout(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_WARN(logger, "send protocol timeout on fd="<<fd<<", info='"<<context->Info<<"'");
	
	return ;
}

void GameRoom::OnSocketFinished(int32_t fd)
{
	//Add Your Code Here
	LOG_INFO(logger, "socket finished. fd="<<fd);
	
	//close it?
	Socket::Close(fd);
	if(fd == m_InterfaceFD)
		m_InterfaceFD = -1;

	return ;
}


void GameRoom::OnTimeout(uint64_t nowtime_ms)
{
	//检查超时的任务
	LOG_DEBUG(logger, "GameRoom timer timeout:");
	//发送RoomInfo信息到Interface
	if(m_InterfaceFD < 0)
	{
		m_InterfaceFD = Socket::Connect(m_InterfacePort, m_InterfaceIP.c_str(), false, 2000);
		if(m_InterfaceFD < 0)
		{
			LOG_ERROR(logger, "connect to GameInterface failed. Interface="<<m_InterfaceIP<<":"<<m_InterfacePort);
			return ;
		}
		LOG_INFO(logger, "connect to GameInterface succ. Interface="<<m_InterfaceIP<<":"<<m_InterfacePort<<", fd="<<m_InterfaceFD);
	}

	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "ReportRoomInfo";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)ReportRoomInfo);
	send_kvdata.SetValue(KEY_RoomID, m_ID);
	send_kvdata.SetValue(KEY_RoomIP, m_IP);
	send_kvdata.SetValue(KEY_RoomPort, m_Port);
	send_kvdata.SetValue(KEY_ClientNum, m_ClientNum);
	send_kvdata.SetValue(KEY_TableNum, m_TableNum);

	uint32_t header_size = m_ProtocolFactory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();
	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	//Set NumArray
	int buf_size = sizeof(int)*m_TableNum;
	send_context->CheckSize(KVData::SizeBytes(buf_size);
	char *data_buffer = send_context->Buffer+send_context->Size;
	KVBuffer kv_buffer = KVData::BeginWrite(data_buffer, KEY_NumArray, true);
	char *num_array = kv_buffer.second;
	for(int i=0; i<m_TableNum; ++i)
		num_array[i] = htonl(m_Tables[i].CurPlayerNum());
	send_context->Size += KVData::EndWrite(kv_buffer, buf_size);

	//编译头部
	m_ProtocolFactory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(SendProtocol(m_InterfaceFD, send_context) == false)
	{
		LOG_ERROR(logger, "send ReportRoomInfo to framework failed.fd="<<m_InterfaceFD);
		DeleteProtocolContext(send_context);
		return ;
	}
	LOG_DEBUG(logger, "send ReportRoomInfo to framework succ.fd="<<m_InterfaceFD);
	return ;
}
