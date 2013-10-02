/*
 * GameRoom.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameRoom.h"

IMPL_LOGGER(GameRoom, logger);

bool GameRoom::Start()
{
	//Add Your Code Here
	
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
