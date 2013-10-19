/*
 * GameRoom.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameRoom.h"
#include <assert.h>
#include <netinet/in.h>

#include "../KeyDefine.h"

#include "Socket.h"
#include "ConfigReader.h"
using namespace easynet;
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

	m_NeedNum = 4;  //游戏需要几个玩家

	for(int i=0; i<m_TableNum; ++i)
	{
		TractorTable table(this, i, m_PackNum, m_NeedNum);
		m_Tables.push_back(table);
	}

	//Interface
	m_InterfaceIP = gConfigReader.GetValue("InterfaceIP", "");
	assert(m_InterfaceIP.size() > 0);
	m_InterfacePort = gConfigReader.GetValue("InterfacePort", -1);
	assert(m_InterfacePort > 0);

	m_InterfaceFD = -1;

	int listen_fd = Listen(m_Port, m_IP.c_str());
	if(listen_fd < 0)
	{
		LOG_ERROR(logger, "listen failed on: "<<m_IP<<":"<<m_Port);
		assert(0);
	}
	LOG_INFO(logger, "listen succ on: "<<m_IP<<":"<<m_Port);

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
	return 720000;
}

int32_t GameRoom::GetMaxConnections()
{
	return 1000;
}

bool GameRoom::OnReceiveProtocol(int32_t fd, ProtocolContext *context, bool &detach_context)
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
	case PRO_IntoRoom:
		return OnIntoRoom(fd, kvdata);
		break;
	case PRO_LeaveRoom:
		return OnLeaveRoom(fd, kvdata);
		break;
	case PRO_AddGame:
		return OnAddGame(fd, kvdata);
		break;
	case PRO_QuitGame:
		return OnQuitGame(fd, kvdata);
		break;
	case PRO_StartGame:
		return OnStartGame(fd, kvdata);
		break;
	default:
		LOG_ERROR(logger, "unknow protocol type="<<protocol<<". fd="<<fd);
		return false;
		break;
	}
	return true;
}

void GameRoom::OnSendSucc(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_DEBUG(logger, "send protocol succ on fd="<<fd<<", info='"<<context->Info<<"'");
	DeleteProtocolContext(context);
	return ;
}

void GameRoom::OnSendError(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_ERROR(logger, "send protocol failed on fd="<<fd<<", info='"<<context->Info<<"'");
	DeleteProtocolContext(context);
	return ;
}

void GameRoom::OnSendTimeout(int32_t fd, ProtocolContext *context)
{
	//Add Your Code Here
	LOG_WARN(logger, "send protocol timeout on fd="<<fd<<", info='"<<context->Info<<"'");
	DeleteProtocolContext(context);
	return ;
}

void GameRoom::OnSocketFinished(int32_t fd)
{
	//Add Your Code Here
	LOG_INFO(logger, "socket finished. fd="<<fd);
	
	//close it?
	Socket::Close(fd);
	if(fd == m_InterfaceFD)
	{
		m_InterfaceFD = -1;
	}
	else
	{
		{
		map<int, int>::iterator it = m_FDClientMap.find(fd);
		if(it != m_FDClientMap.end())
		{
			LOG_DEBUG(logger, "OnSocketFinished:remove ClientID="<<it->second<<".fd="<<fd);
			m_ClientSet.erase(it->second);
			m_FDClientMap.erase(it);
		}
		}

		{
		PlayerMap::iterator it = m_PlayerMap.find(fd);
		if(it != m_PlayerMap.end())
		{
			//意外断开,退出游戏
			OnAbortQuitGame(it->second);
			m_PlayerMap.erase(it);
		}
		}

		RoomInfoBroadCast();
	}
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

	//计算玩家数量
	int PlayerNum=0;
	PlayerMap::iterator it;
	for(it=m_PlayerMap.begin(); it!=m_PlayerMap.end(); ++it)
	{
		if(it->second.status == STATUS_PLAYING)
			++PlayerNum;
	}

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)PRO_ReportRoomInfo);
	send_kvdata.SetValue(KEY_RoomID, m_ID);
	send_kvdata.SetValue(KEY_RoomIP, m_IP);
	send_kvdata.SetValue(KEY_RoomPort, m_Port);
	send_kvdata.SetValue(KEY_ClientNum,(int)m_ClientSet.size());
	send_kvdata.SetValue(KEY_PlayerNum, PlayerNum);
	send_kvdata.SetValue(KEY_TableNum, m_TableNum);


	ProtocolContext *send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "ReportRoomInfo";

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();
	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(SendProtocol(m_InterfaceFD, send_context) == false)
	{
		LOG_ERROR(logger, "send ReportRoomInfo to framework failed.fd="<<m_InterfaceFD);
		DeleteProtocolContext(send_context);
		return ;
	}
	LOG_DEBUG(logger, "send ReportRoomInfo to framework succ.fd="<<m_InterfaceFD);
	return ;
}

bool GameRoom::OnIntoRoom(int fd, KVData *kvdata)
{
	int RoomID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnIntoRoom: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnIntoRoom: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnIntoRoom: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID != m_ID)
	{
		LOG_ERROR(logger, "OnIntoRoom: room id invalid. recv RoomID="<<RoomID
				<<",my RoomID="<<m_ID
				<<",fd="<<fd);
		return false;
	}
	LOG_DEBUG(logger, "OnIntoRoom: ClientID="<<ClientID<<",ClientName="<<ClientName<<" into room="<<RoomID<<".fd="<<fd);

	//统计人数
	map<int,int>::iterator it = m_FDClientMap.find(fd);
	if(it == m_FDClientMap.end())
	{
		m_FDClientMap.insert(std::make_pair(fd, ClientID));
		m_ClientSet.insert(ClientID);
	}
	else if(it->second != ClientID)
	{
		m_ClientSet.erase(it->second);
		it->second = ClientID;
	}

	SendRoomInfo(fd);
	return true;
}

bool GameRoom::OnLeaveRoom(int fd, KVData *kvdata)
{
	int RoomID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnLeaveRoom: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnLeaveRoom: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnLeaveRoom: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID != m_ID)
	{
		LOG_ERROR(logger, "OnLeaveRoom: room id invalid. recv RoomID="<<RoomID
				<<",my RoomID="<<m_ID
				<<",fd="<<fd);
		return false;
	}
	LOG_DEBUG(logger, "OnLeaveRoom: ClientID="<<ClientID<<",ClientName="<<ClientName<<" live room="<<RoomID<<".fd="<<fd);

	map<int,int>::iterator it = m_FDClientMap.find(fd);
	if(it != m_FDClientMap.end())
	{
		m_ClientSet.erase(it->second);
		m_FDClientMap.erase(it);
	}

	//RoomInfoBroadCast();
	//关闭连接
	NotifySocketFinish(fd);
	return true;
}

void GameRoom::SendRoomInfo(int fd)
{
	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "RoomInfo";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)PRO_RoomInfoBroadCast);
	send_kvdata.SetValue(KEY_RoomID, m_ID);
	send_kvdata.SetValue(KEY_NeedNum, m_NeedNum);
	send_kvdata.SetValue(KEY_ClientNum, (int)m_ClientSet.size());
	send_kvdata.SetValue(KEY_TableNum, m_TableNum);

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();
	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	//Set NumArray
	int buf_size = sizeof(int)*m_TableNum;
	send_context->CheckSize(KVData::SizeBytes(buf_size));
	char *data_buffer = send_context->Buffer+send_context->Size;
	KVBuffer kv_buffer = KVData::BeginWrite(data_buffer, KEY_Array, true);
	int *num_array = (int*)kv_buffer.second;
	for(int i=0; i<m_TableNum; ++i)
		num_array[i] = htonl(m_Tables[i].GetPlayerArray());
	send_context->Size += KVData::EndWrite(kv_buffer, buf_size);
	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);

	if(SendProtocol(fd, send_context) == false)
	{
		LOG_ERROR(logger, "RoomInfoBroadCast: send GetRoomInfoRsp to framework failed.fd="<<fd);
		DeleteProtocolContext(send_context);
	}
	else
		LOG_DEBUG(logger, "RoomInfoBroadCast: sent GetRoomInfoRsp to framework succ. fd="<<fd);

}

void GameRoom::RoomInfoBroadCast()
{
	LOG_DEBUG(logger, "RoomInfoBroadCast");

	map<int,int>::iterator it;
	//广播房间信息
	for(it=m_FDClientMap.begin(); it!=m_FDClientMap.end(); ++it)
	{
		if(m_PlayerMap.find(it->first) != m_PlayerMap.end())  //在房间但没有选择桌子的用户
			continue;
		SendRoomInfo(it->first);
	}
}

bool GameRoom::OnAddGame(int fd, KVData *kvdata)
{
	int RoomID;
	int TableID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnAddGame: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_TableID, TableID))
	{
		LOG_ERROR(logger, "OnAddGame: get TableID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnAddGame: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnAddGame: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID!=m_ID || TableID<0 || TableID>=m_TableNum)
	{
		LOG_ERROR(logger, "OnAddGame: RoomID or TableID invalid. recv RoomID="<<RoomID
				<<",recv TableID="<<TableID
				<<",my RoomID="<<m_ID
				<<",fd="<<fd);
		return false;
	}
	LOG_DEBUG(logger, "OnAddGame: ClientID="<<ClientID<<",ClientName="<<ClientName<<" Into Table="<<TableID<<".fd="<<fd);

	//统计人数
	{
	map<int,int>::iterator it = m_FDClientMap.find(fd);
	if(it == m_FDClientMap.end())
	{
		m_FDClientMap.insert(std::make_pair(fd, ClientID));
		m_ClientSet.insert(ClientID);
	}
	else if(it->second != ClientID)
	{
		m_ClientSet.erase(it->second);
		it->second = ClientID;
	}
	}

	//add a new player
	PlayerMap::iterator it = m_PlayerMap.find(fd);
	if(it == m_PlayerMap.end())  //new player
	{
		Player temp_player;
		temp_player.client_id = ClientID;
		temp_player.client_name = ClientName;
		temp_player.index = -1;
		temp_player.table_id = TableID;
		temp_player.status = STATUS_INVALID;
		temp_player.fd = fd;

		std::pair<PlayerMap::iterator, bool> ret = m_PlayerMap.insert(std::make_pair(fd, temp_player));
		assert(ret.second == true);
		it = ret.first;
	}
	Player *player = &it->second;
	assert(player->table_id == TableID);
	bool ret = m_Tables[TableID].OnAddGame(player);

	if(player->status > STATUS_AUDIENCE)
		RoomInfoBroadCast();
	return ret;
}

bool GameRoom::OnQuitGame(int fd, KVData *kvdata)
{
	int RoomID;
	int TableID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnQuitGame: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_TableID, TableID))
	{
		LOG_ERROR(logger, "OnQuitGame: get TableID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnQuitGame: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnQuitGame: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID!=m_ID || TableID<0 || TableID>=m_TableNum)
	{
		LOG_ERROR(logger, "OnQuitGame: RoomID or TableID invalid. recv RoomID="<<RoomID
				<<",recv TableID="<<TableID
				<<",my RoomID="<<m_ID
				<<",fd="<<fd);
		return false;
	}

	LOG_DEBUG(logger, "OnQuitGame: ClientID="<<ClientID<<",ClientName="<<ClientName<<",TableID="<<TableID<<".fd="<<fd);

	PlayerMap::iterator it = m_PlayerMap.find(fd);
	if(it != m_PlayerMap.end())
	{
		Player *player = &it->second;
		assert(player->table_id == TableID);
		m_Tables[TableID].OnQuitGame(player);
		m_PlayerMap.erase(fd);

		RoomInfoBroadCast();
	}
	else
	{
		LOG_ERROR(logger,"OnQuitGame: not found player.fd="<<fd);
	}

	return true;
}

void GameRoom::OnAbortQuitGame(Player &player)  //意外退出游戏
{
	LOG_ERROR(logger, "OnAbortQuitGame:ClientID="<<player.client_id<<",TableID="<<player.table_id<<".fd="<<player.fd);
	if(player.table_id<0 || player.table_id>=m_TableNum)
		return ;
	m_Tables[player.table_id].OnQuitGame(&player);
}

bool GameRoom::OnStartGame(int fd, KVData *kvdata)
{
	int RoomID;
	int TableID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnStartGame: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_TableID, TableID))
	{
		LOG_ERROR(logger, "OnStartGame: get TableID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnStartGame: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnStartGame: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID!=m_ID || TableID<0 || TableID>=m_TableNum)
	{
		LOG_ERROR(logger, "OnStartGame: RoomID or TableID invalid. recv RoomID="<<RoomID
				<<",recv TableID="<<TableID
				<<",my RoomID="<<m_ID
				<<",fd="<<fd);
		return false;
	}

	LOG_DEBUG(logger, "OnStartGame: ClientID="<<ClientID<<",ClientName="<<ClientName<<",TableID="<<TableID<<".fd="<<fd);

	PlayerMap::iterator it = m_PlayerMap.find(fd);
	if(it != m_PlayerMap.end())
	{
		Player *player = &it->second;
		m_Tables[TableID].OnStartGame(player);
	}
	else
	{
		LOG_ERROR(logger, "OnstartGame: Can't find player. ClientID="<<ClientID<<",ClientName="<<ClientName<<",TableID="<<TableID<<".fd="<<fd);
	}

	return true;
}

