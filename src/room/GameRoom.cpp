/*
 * GameRoom.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameRoom.h"

#include <assert.h>

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
	//Socket::Close(fd);

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


}
