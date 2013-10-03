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

	m_PlayerNum = 4;

	for(int i=0; i<m_TableNum; ++i)
	{
		TractorTable table(m_PackNum, m_PlayerNum);
		table.Timer.Init(this, i);
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
	KVData *kvdata = (KVData*)context->protocol;

	int protocol;
	if(!kvdata->GetValue(KEY_Protocol, protocol))
	{
		LOG_ERROR(logger, "get protocol type failed. fd="<<fd);
		return false;
	}

	switch(protocol)
	{
	case IntoRoom:
		return OnIntoRoom(fd, kvdata);
		break;
	case OutRoom:
		return OnOutRoom(fd, kvdata);
		break;
	case GetRoomInfo:
		return OnGetRoomInfo(fd, kvdata);
		break;
	case AddGame:
		return OnAddGame(fd, kvdata);
		break;
	case QuitGame:
		return OnQuitGame(fd, kvdata);
		break;
	case StartGame:
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

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
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
		num_array[i] = htonl(m_Tables[i].CurPlayerNum);
	send_context->Size += KVData::EndWrite(kv_buffer, buf_size);

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
		LOG_ERROR(logger, "OnIntoRoom: room_id invalid. recv RoomID="<<RoomID<<",my RoomID="<<m_ID<<",fd="<<fd);
		return false;
	}

	LOG_DEBUG(logger, "OnIntoRoom: ClientID="<<ClientID<<",ClientName="<<ClientName<<" Into Room. fd="<<fd);

	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "IntoRoomRsp";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)IntoRoomRsp);
	string WelcomeMsg="Welcome "+ClientName;
	send_kvdata.SetValue(KEY_WelcomeMsg, WelcomeMsg);
	send_kvdata.SetValue(KEY_RoomID, RoomID);
	send_kvdata.SetValue(KEY_ClientNum, m_ClientNum+1);
	send_kvdata.SetValue(KEY_TableNum, m_TableNum);

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
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
		num_array[i] = htonl(m_Tables[i].CurPlayerNum);
	send_context->Size += KVData::EndWrite(kv_buffer, buf_size);

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(SendProtocol(fd, send_context) == false)
	{
		LOG_ERROR(logger, "send IntoRoomRsp to framework failed.fd="<<fd);
		DeleteProtocolContext(send_context);
		return false;
	}

	++m_ClientNum;
	LOG_DEBUG(logger, "send IntoRoomRsp to framework succ.cur ClientNum="<<m_ClientNum<<",fd="<<fd);
	return true;
}

bool GameRoom::OnOutRoom(int fd, KVData *kvdata)
{
	int RoomID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnOutRoom: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnOutRoom: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnOutRoom: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID != m_ID)
	{
		LOG_ERROR(logger, "OnOutRoom: room_id invalid. recv RoomID="<<RoomID<<",my RoomID="<<m_ID<<",fd="<<fd);
		return false;
	}

	--m_ClientNum;
	LOG_DEBUG(logger, "OnOutRoom: ClientID="<<ClientID<<",ClientName="<<ClientName<<" Out Room. cur ClientNum="<<m_ClientNum<<",fd="<<fd);

	return true;
}

bool GameRoom::OnGetRoomInfo(int fd, KVData *kvdata)
{
	int RoomID;
	int ClientID;
	string ClientName;

	if(!kvdata->GetValue(KEY_RoomID, RoomID))
	{
		LOG_ERROR(logger, "OnGetRoomInfo: get RoomID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientID, ClientID))
	{
		LOG_ERROR(logger, "OnGetRoomInfo: get ClientID failed. fd="<<fd);
		return false;
	}
	if(!kvdata->GetValue(KEY_ClientName, ClientName))
	{
		LOG_ERROR(logger, "OnGetRoomInfo: get ClientName failed. fd="<<fd);
		return false;
	}

	if(RoomID != m_ID)
	{
		LOG_ERROR(logger, "OnGetRoomInfo: room_id invalid. recv RoomID="<<RoomID<<",my RoomID="<<m_ID<<",fd="<<fd);
		return false;
	}
	LOG_DEBUG(logger, "OnGetRoomInfo: ClientID="<<ClientID<<",ClientName="<<ClientName<<",fd="<<fd);

	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "GetRoomInfoRsp";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)GetRoomInfoRsp);
	send_kvdata.SetValue(KEY_RoomID, RoomID);
	send_kvdata.SetValue(KEY_ClientNum, m_ClientNum);
	send_kvdata.SetValue(KEY_TableNum, m_TableNum);

	IProtocolFactory *protocol_factory = GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
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
		num_array[i] = htonl(m_Tables[i].CurPlayerNum);
	send_context->Size += KVData::EndWrite(kv_buffer, buf_size);

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(SendProtocol(fd, send_context) == false)
	{
		LOG_ERROR(logger, "OnGetRoomInfo: send GetRoomInfoRsp to framework failed.fd="<<fd);
		DeleteProtocolContext(send_context);
		return false;
	}
	LOG_DEBUG(logger, "OnGetRoomInfo: send IntoRoomRsp to framework succ.cur ClientNum="<<m_ClientNum<<",fd="<<fd);
	return true;
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

	//add a new player
	PlayerMap::iterator it = m_PlayerMap.find(ClientID);
	if(it == m_PlayerMap.end())  //new player
	{
		Player temp_player;
		temp_player.client_id = ClientID;
		temp_player.index = -1;
		temp_player.table_id = TableID;
		temp_player.status = STATUS_INVALID;

		std::pair<PlayerMap::iterator, bool> ret = m_PlayerMap.insert(std::make_pair(ClientID, temp_player));
		assert(ret.second == true);
		it = ret.first;
	}
	Player *player = &it->second;
	assert(player->table_id == TableID);
	TractorTable &table = m_Tables[TableID];

	if(table.CurPlayerNum < m_PlayerNum)  //人数未满,成为玩家
	{
		player->status = STATUS_WAIT;
		player->index = table.GetPlayerIndex();
		assert(player->index != -1);
		table.IndexPlayer[player->index] = player;
		++table.CurPlayerNum;

		//如果之前是旁观者,则删除
		table.Audience.erase(ClientID);
	}
	else  //成为旁观者
	{
		player->status = STATUS_AUDIENCE;
		player->index = -1;
		table.Audience.insert(std::make_pair(ClientID, player));
	}

	ProtocolContext *send_context = NULL;
	send_context = NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "AddGameRsp";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)AddGameRsp);
	string WelcomeMsg="Welcome "+ClientName;
	send_kvdata.SetValue(KEY_WelcomeMsg, WelcomeMsg);
	send_kvdata.SetValue(KEY_Status, (int)player->status);

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
		LOG_ERROR(logger, "OnAddGame: send IntoTableRsp to framework failed.fd="<<fd);
		DeleteProtocolContext(send_context);
		return false;
	}

	LOG_DEBUG(logger, "OnAddGame: end AddGameRsp to framework succ.ClientName="<<ClientName<<",ClientID="<<ClientID<<",fd="<<fd);
	return true;
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

	PlayerMap::iterator it = m_PlayerMap.find(ClientID);
	if(it != m_PlayerMap.end())
	{
		Player *player = it->second;
		assert(player->table_id == TableID);
		TractorTable &table = m_Tables[TableID];
		if(player->status == STATUS_AUDIENCE)
		{
			table.Audience.erase(ClientID);
			//TODO:其他玩家设置为STATUS_WAIT状态
		}
		else
			table.IndexPlayer[player->index] = NULL;
		m_PlayerMap.erase(it);
	}
	return true;
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

	PlayerMap::iterator it = m_PlayerMap.find(ClientID);
	if(it != m_PlayerMap.end())
	{
		Player &player = it->second;
		assert(player.table_id==TableID && player.status==STATUS_WAIT);
		player.status = STATUS_PLAYING;

		//判断所有的玩家是否都是PLAYING状态
		int i;
		for(i=0; i<m_PlayerNum; ++i)
		{
			if(m_Tables[TableID].IndexPlayer[i]==NULL
				|| m_Tables[TableID].IndexPlayer[i]->status!=STATUS_PLAYING)
				break;
		}

		//所有玩家都进入PLAYING状态
		if(i >= m_PlayerNum)
		{
			//启动时钟开始发牌
			IEventServer *event_server = GetEventServer();
			IEventHandler *timer_handler = &m_Tables[TableID].Timer;
			if(!event_server->AddTimer(timer_handler, 500, true))
			{
				LOG_ERROR(logger, "OnStartGame: all player are ready but add deal timer failed. TableID="<<TableID<<". fd="<<fd);
				assert(0);
			}
			LOG_INFO(logger, "OnStartGame: all player are ready add deal timer succ. TableID="<<TableID<<". fd="<<fd);
		}
	}
	else
	{
		LOG_ERROR(logger, "OnStartGame: not found ClientID="<<ClientID<<". fd="<<fd);
	}

	return true;
}

//为table_id的桌子发牌
bool GameRoom::OnTableTimerTimeout(int table_id)
{

	return true;
}
