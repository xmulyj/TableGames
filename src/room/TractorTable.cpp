/*
 * TractorTable.cpp
 *
 *  Created on: Sep 30, 2013
 *      Author: tim
 */

#include "TractorTable.h"
#include <assert.h>
#include <netinet/in.h>

#include "GameRoom.h"
#include "../KeyDefine.h"

IMPL_LOGGER(TractorTable, logger);

#define DEAL_TIMEOUT  500    //每轮发牌时间(ms)

TractorTable::TractorTable(GameRoom *game_room, int table_id, int n_poker/*=1*/, int n_player/*=4*/)
	:m_GameRoom(game_room)
	,m_Poker(n_poker)
	,m_NeedNum(n_player)
	,m_TableID(table_id)
{
	for(int i=0; i<10; ++i)
		m_Player[i] = NULL;
	m_PlayerNum = 0;
	m_Dealer = 0;
	m_PokerNum = 0;

	if(n_poker%2 == 1)
		m_KeepPokerNum = 6;
	else
		m_KeepPokerNum = 8;

}

//时钟超时
void TractorTable::OnTimeout(uint64_t nowtime_ms)
{
	if(m_PlayerNum < m_NeedNum)
	{
		LOG_DEBUG(logger, "Table OnTimeout but player no enough.TableID="<<m_TableID);
		return ;
	}


	LOG_DEBUG(logger, "Table OnTimeout: TableID="<<m_TableID<<",remain poker="<<m_Poker.Remain());

	int i;
	for(i=0; i<m_NeedNum; ++i)
	{
		assert(m_Player[i] != NULL);
		if(m_Player[i]->status != STATUS_PLAYING)
			return ;
	}

	bool finished = false;  //最后一轮
	int start = m_Dealer<0?0:m_Dealer;  //从庄家开始发牌
	for(i=0; i<m_NeedNum; ++i)
	{
		m_Player[start]->poker.push_back(m_Poker.Deal());
		start = (start+1)%m_NeedNum;
	}
	++m_PokerNum;

	//庄家获得N张底牌
	if(m_Poker.Remain() == m_KeepPokerNum)
	{
		finished = true;
		for(i=0; i<m_KeepPokerNum; ++i)
			m_Player[m_Dealer]->poker.push_back(m_Poker.Deal());
	}

	int poker[20];
	int CardFlag = 0;
	for(i=0; i<m_NeedNum; ++i)
	{
		poker[0] = htonl(m_Player[i]->poker.back());
		if(finished)  //最后一轮
		{
			CardFlag = 1;
			if(i == m_Dealer)  //给庄家发底牌
			{
				CardFlag = 1+m_KeepPokerNum;
				int index = m_Player[i]->poker.size()-1;
				for(int j=1; j<=m_KeepPokerNum;++j)
					poker[j] = htonl(m_Player[i]->poker[--index]);
			}
		}

		string num_array;
		if(CardFlag == 0)
			num_array.assign((const char*)poker, sizeof(int));
		else
			num_array.assign((const char*)poker, sizeof(int)*CardFlag);

		//发扑克牌
		KVData send_kvdata(true);
		send_kvdata.SetValue(KEY_Protocol, PRO_DealPoker);
		send_kvdata.SetValue(KEY_CardFlag, CardFlag);
		send_kvdata.SetValue(KEY_Array, num_array);

		ProtocolContext *send_context = NULL;
		send_context = m_GameRoom->NewProtocolContext();
		assert(send_context != NULL);
		send_context->type = DTYPE_BIN;
		send_context->Info = "DealPoker";

		IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
		uint32_t header_size = protocol_factory->HeaderSize();
		uint32_t body_size = send_kvdata.Size();
		send_context->CheckSize(header_size+body_size);
		send_kvdata.Serialize(send_context->Buffer+header_size);
		send_context->Size = header_size+body_size;

		//编译头部
		protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
		if(m_GameRoom->SendProtocol(m_Player[i]->fd, send_context) == false)
		{
			LOG_ERROR(logger, "OnTimeout: send DealPoker to framework failed. Player:client_id=="<<m_Player[i]->client_id<<".fd="<<m_Player[i]->fd);
			m_GameRoom->DeleteProtocolContext(send_context);
			//assert(0);
		}
	}

	//通知观众
	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, PRO_DealPoker);
	send_kvdata.SetValue(KEY_CardFlag, CardFlag);

	PPlayerMap::iterator it;
	for(it=m_Audience.begin(); it!=m_Audience.end(); ++it)
	{
		ProtocolContext *send_context = NULL;
		send_context = m_GameRoom->NewProtocolContext();
		assert(send_context != NULL);
		send_context->type = DTYPE_BIN;
		send_context->Info = "DealPoker";

		IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
		uint32_t header_size = protocol_factory->HeaderSize();
		uint32_t body_size = send_kvdata.Size();
		send_context->CheckSize(header_size+body_size);
		send_kvdata.Serialize(send_context->Buffer+header_size);
		send_context->Size = header_size+body_size;

		//编译头部
		protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
		if(m_GameRoom->SendProtocol(it->second->fd, send_context) == false)
		{
			if(it == m_Audience.end())
				LOG_ERROR(logger, "OnTimeout: send DealPoker to framework failed. Player:client_id=="<<it->second->client_id<<".fd="<<it->second->fd);
			else
				LOG_ERROR(logger, "OnTimeout: send DealPoker to framework failed. Audience:client_id="<<it->second->client_id<<".fd="<<it->second->fd);
			m_GameRoom->DeleteProtocolContext(send_context);
		}
	}

	if(!finished)
	{
		//时钟未完成,重新添加
		IEventServer *event_server = m_GameRoom->GetEventServer();
		if(!event_server->AddTimer(this, DEAL_TIMEOUT, false))
			assert(0);
	}
	else
	{
		LOG_INFO(logger, "finish to deal poker. TableID="<<m_TableID);
	}
}

int TractorTable::GetPlayerIndex()
{
	if(m_PlayerNum >= m_NeedNum)
		return -1;
	for(int i=0; i<m_NeedNum; ++i)
		if(m_Player[i] == NULL)
			return i;
	return -1;
}

bool TractorTable::OnAddGame(Player *player)
{
	if(player->status <= STATUS_AUDIENCE)
	{
		if(m_PlayerNum < m_NeedNum)   //人数未满,成为玩家
		{
			player->status = STATUS_WAIT;
			player->index = GetPlayerIndex();
			assert(player->index != -1);
			m_Player[player->index] = player;
			++m_PlayerNum;

			//如果之前是旁观者,则删除
			m_Audience.erase(player->client_id);
		}
		else  //成为旁观者
		{
			player->status = STATUS_AUDIENCE;
			player->index = -1;
			m_Audience.insert(std::make_pair(player->client_id, player));
		}
	}

	char buffer[100];
	snprintf(buffer, 100, "Player[uid=%d] add game", player->client_id);
	string msg(buffer);
	SendAddGameRsp(player->fd, msg);
	AddGameBroadCast(player);

	return true;
}

bool TractorTable::OnQuitGame(Player *player)
{
	if(player->status == STATUS_AUDIENCE)
	{
		m_Audience.erase(player->client_id);
	}
	else
	{
		m_Player[player->index] = NULL;
		--m_PlayerNum;
		//其他玩家设置为STATUS_WAIT状态
		for(int i=0; i<m_NeedNum; ++i)
		{
			if(m_Player[i] != NULL)
			{
				m_Player[i]->status = STATUS_WAIT;
				m_Player[i]->poker.clear();
			}
		}
	}
	m_PokerNum = 0;
	QuitGameBroadCast(player);

	return true;
}

bool TractorTable::OnStartGame(Player *player)
{
	assert(player->table_id==m_TableID && m_Player[player->index]==player && player->status==STATUS_WAIT);
	player->status = STATUS_PLAYING;

	StartGameBroadCast(player);

	//判断所有的玩家是否都是PLAYING状态
	int i;
	for(i=0; i<m_NeedNum; ++i)
	{
		if(m_Player[i]==NULL || m_Player[i]->status!=STATUS_PLAYING)
			break;
	}

	//所有玩家都进入PLAYING状态,开始发牌
	if(i >= m_NeedNum)
	{
		//所有玩家先清牌
		for(i=0; i<m_NeedNum; ++i)
			m_Player[i]->poker.clear();

		m_PokerNum = 0;
		m_Poker.Shuffle();
		//启动时钟开始发牌
		IEventServer *event_server = m_GameRoom->GetEventServer();
		if(!event_server->AddTimer(this, DEAL_TIMEOUT, false))
		{
			LOG_ERROR(logger, "OnStartGame: all player are ready but add deal timer failed. TableID="<<m_TableID<<". fd="<<player->fd);
			assert(0);
		}
		LOG_INFO(logger, "OnStartGame: all player are ready and add deal timer succ. TableID="<<m_TableID<<". fd="<<player->fd);
	}

	return true;
}

void TractorTable::SendAddGameRsp(int fd, string &msg)
{
	LOG_DEBUG(logger, "SendAddGameRsp");

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)PRO_AddGameRsp);
	send_kvdata.SetValue(KEY_Message, msg);
	send_kvdata.SetValue(KEY_CurLevel, m_CulLevel);
	send_kvdata.SetValue(KEY_CurColor, m_CulColor);
	send_kvdata.SetValue(KEY_CurDealer, m_Dealer);
	send_kvdata.SetValue(KEY_PokerNum, m_PokerNum);
	send_kvdata.SetValue(KEY_PlayerNum, m_PlayerNum);
	send_kvdata.SetValue(KEY_AudienceNum, (int)m_Audience.size());
	send_kvdata.SetValue(KEY_ScoreNum, (int)m_Score.size());

	IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();

	ProtocolContext *send_context = m_GameRoom->NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "AddGameRsp";

	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	//设置Array
	int size_array = sizeof(int)*3*(m_PlayerNum+m_Audience.size());
	send_context->CheckSize(KVData::SizeBytes(size_array));

	KVBuffer kv_buf = KVData::BeginWrite(send_context->Buffer+send_context->Size, KEY_Array, true);
	int *num_array = (int*)kv_buf.second;
	for(int i=0; i<m_NeedNum; ++i)
	{
		if(m_Player[i] == NULL)
			continue;
		*num_array++ = htonl(m_Player[i]->client_id);
		*num_array++ = htonl((int)m_Player[i]->status);
		*num_array++ = htonl(m_Player[i]->index);
	}
	for(PPlayerMap::iterator it=m_Audience.begin(); it!=m_Audience.end(); ++it)
	{
		*num_array++ = htonl(it->second->client_id);
		*num_array++ = htonl(it->second->status);
		*num_array++ = htonl(-1);
	}
	send_context->Size += KVData::EndWrite(kv_buf, size_array);

	//设置ScoreArray
	if(m_Score.size() > 0)
	{
		int size_array = sizeof(int)*m_Score.size();
		send_context->CheckSize(KVData::SizeBytes(size_array));

		KVBuffer kv_buf = KVData::BeginWrite(send_context->Buffer+send_context->Size, KEY_ScoreArray, true);
		int *num_array = (int*)kv_buf.second;
		for(int i=0; i<m_Score.size(); ++i)
			*num_array++ = htonl(m_Score[i]);
		send_context->Size += KVData::EndWrite(kv_buf, size_array);
	}

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(m_GameRoom->SendProtocol(fd, send_context) == false)
	{
		LOG_ERROR(logger, "SendAddGameRsp: send SendAddGameRsp to framework failed.fd="<<fd);
		m_GameRoom->DeleteProtocolContext(send_context);
	}
	else
	{
		LOG_DEBUG(logger, "SendAddGameRsp: send SendAddGameRsp to framework succ.fd="<<fd);
	}
}

void TractorTable::AddGameBroadCast(Player *player)
{
	//获取桌子中,除player的所有其他人
	vector<int> fd_vector;
	for(int i=0; i<m_NeedNum; ++i)
	{
		if(m_Player[i]!=NULL && m_Player[i] != player)
			fd_vector.push_back(m_Player[i]->fd);
	}
	PPlayerMap::iterator it;
	for(it=m_Audience.begin(); it!=m_Audience.end(); ++it)
	{
		if(it->second != player)
			fd_vector.push_back(it->second->fd);
	}

	KVData send_kvdata(true);
	for(int i=0; i<fd_vector.size(); ++i)
	{
		send_kvdata.Clear();
		send_kvdata.SetValue(KEY_Protocol, (int)PRO_AddGameBroadCast);
		send_kvdata.SetValue(KEY_ClientID, player->client_id);
		send_kvdata.SetValue(KEY_ClientName, player->client_name);
		send_kvdata.SetValue(KEY_PosIndex, player->index);

		IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
		uint32_t header_size = protocol_factory->HeaderSize();
		uint32_t body_size = send_kvdata.Size();

		ProtocolContext *send_context = m_GameRoom->NewProtocolContext();
		assert(send_context != NULL);
		send_context->type = DTYPE_BIN;
		send_context->Info = "AddGameBroadCast";

		send_context->CheckSize(header_size+body_size);
		send_kvdata.Serialize(send_context->Buffer+header_size);
		send_context->Size = header_size+body_size;

		protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
		if(m_GameRoom->SendProtocol(fd_vector[i], send_context) == false)
		{
			LOG_ERROR(logger, "AddGameBroadCast: send AddGameBroadCast to framework failed.fd="<<fd_vector[i]);
			m_GameRoom->DeleteProtocolContext(send_context);
		}
		else
		{
			LOG_DEBUG(logger, "AddGameBroadCast: send AddGameBroadCast to framework succ.fd="<<fd_vector[i]);
		}
	}
}

void TractorTable::QuitGameBroadCast(Player *player)
{
	//获取桌子中,除player的所有其他人
	vector<int> fd_vector;
	for(int i=0; i<m_NeedNum; ++i)
	{
		if(m_Player[i]!=NULL && m_Player[i] != player)
			fd_vector.push_back(m_Player[i]->fd);
	}
	PPlayerMap::iterator it;
	for(it=m_Audience.begin(); it!=m_Audience.end(); ++it)
	{
		if(it->second != player)
			fd_vector.push_back(it->second->fd);
	}

	KVData send_kvdata(true);
	for(int i=0; i<fd_vector.size(); ++i)
	{
		send_kvdata.Clear();
		send_kvdata.SetValue(KEY_Protocol, (int)PRO_QuitGameBroadCast);
		send_kvdata.SetValue(KEY_ClientID, player->client_id);
		send_kvdata.SetValue(KEY_ClientName, player->client_name);

		IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
		uint32_t header_size = protocol_factory->HeaderSize();
		uint32_t body_size = send_kvdata.Size();

		ProtocolContext *send_context = m_GameRoom->NewProtocolContext();
		assert(send_context != NULL);
		send_context->type = DTYPE_BIN;
		send_context->Info = "QuitGameBroadCast";

		send_context->CheckSize(header_size+body_size);
		send_kvdata.Serialize(send_context->Buffer+header_size);
		send_context->Size = header_size+body_size;

		protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
		if(m_GameRoom->SendProtocol(fd_vector[i], send_context) == false)
		{
			LOG_ERROR(logger, "QuitGameBroadCast: send QuitGameBroadCast to framework failed.fd="<<fd_vector[i]);
			m_GameRoom->DeleteProtocolContext(send_context);
		}
		else
		{
			LOG_DEBUG(logger, "QuitGameBroadCast: send QuitGameBroadCast to framework succ.fd="<<fd_vector[i]);
		}
	}
}

void TractorTable::StartGameBroadCast(Player *player)
{
	//获取桌子中,除player的所有其他人
	vector<int> fd_vector;
	for(int i=0; i<m_NeedNum; ++i)
	{
		if(m_Player[i]!=NULL && m_Player[i] != player)
			fd_vector.push_back(m_Player[i]->fd);
	}
	PPlayerMap::iterator it;
	for(it=m_Audience.begin(); it!=m_Audience.end(); ++it)
	{
		if(it->second != player)
			fd_vector.push_back(it->second->fd);
	}

	KVData send_kvdata(true);
	for(int i=0; i<fd_vector.size(); ++i)
	{
		send_kvdata.Clear();
		send_kvdata.SetValue(KEY_Protocol, (int)PRO_StartGameBroadCast);
		send_kvdata.SetValue(KEY_ClientID, player->client_id);
		send_kvdata.SetValue(KEY_ClientName, player->client_name);

		IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
		uint32_t header_size = protocol_factory->HeaderSize();
		uint32_t body_size = send_kvdata.Size();

		ProtocolContext *send_context = m_GameRoom->NewProtocolContext();
		assert(send_context != NULL);
		send_context->type = DTYPE_BIN;
		send_context->Info = "StartGameBroadCast";

		send_context->CheckSize(header_size+body_size);
		send_kvdata.Serialize(send_context->Buffer+header_size);
		send_context->Size = header_size+body_size;

		protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
		if(m_GameRoom->SendProtocol(fd_vector[i], send_context) == false)
		{
			LOG_ERROR(logger, "StartGameBroadCast: send StartGameBroadCast to framework failed.fd="<<fd_vector[i]);
			m_GameRoom->DeleteProtocolContext(send_context);
		}
		else
		{
			LOG_DEBUG(logger, "StartGameBroadCast: send StartGameBroadCast to framework succ.fd="<<fd_vector[i]);
		}
	}
}
