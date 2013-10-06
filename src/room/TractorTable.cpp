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
	,m_PlayerNum(n_player)
	,m_TableID(table_id)
{
	for(int i=0; i<10; ++i)
		m_Player[i] = NULL;
	m_CurPlayerNum = 0;
	m_Dealer = -1;

	if(n_poker%2 == 1)
		m_KeepPokerNum = 6;
	else
		m_KeepPokerNum = 8;

}

//时钟超时
void TractorTable::OnTimeout(uint64_t nowtime_ms)
{
	LOG_DEBUG(logger, "OnTimeout: TableID="<<m_TableID);

	if(m_CurPlayerNum < m_PlayerNum)
		return ;

	int i;
	for(i=0; i<m_PlayerNum; ++i)
	{
		assert(m_Player[i] != NULL);
		if(m_Player[i]->status != STATUS_PLAYING)
			return ;
	}

	bool finished = false;
	int start = m_Dealer<0?0:m_Dealer;  //从庄家开始发牌
	for(i=0; i<m_PlayerNum; ++i)
	{
		m_Player[start]->poker.push_back(m_Poker.Deal());
		start = (start+1)%m_PlayerNum;
	}

	//庄家获得N张底牌
	if(m_Poker.Remain() == m_KeepPokerNum)
	{
		finished = true;
		for(i=0; i<m_KeepPokerNum; ++i)
			m_Player[m_Dealer]->poker.push_back(m_Poker.Deal());
	}

	int poker[20];
	for(i=0; i<m_PlayerNum; ++i)
	{
		int status = 0;
		poker[0] = htonl(m_Player[i]->poker.back());
		if(finished)
		{
			status = 1;
			if(i == m_Dealer)
			{
				status = 1+m_KeepPokerNum;
				int index = m_Player[i]->poker.size()-1;
				for(int j=1; j<=m_KeepPokerNum;++j)
					poker[j] = htonl(m_Player[i]->poker[--index]);
			}
		}
		string num_array;
		if(status==0 || status==1)
			num_array.assign((const char*)poker, sizeof(int));
		else
			num_array.assign((const char*)poker, sizeof(int)*status);

		KVData send_kvdata(true);
		send_kvdata.SetValue(KEY_Protocol, DealPoker);
		send_kvdata.SetValue(KEY_Status, status);
		send_kvdata.SetValue(KEY_NumArray, num_array);


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
			LOG_ERROR(logger, "OnAddGame: send DealPoker to framework failed. player="<<m_Player[i]->index<<".fd="<<m_Player[i]->fd);
			m_GameRoom->DeleteProtocolContext(send_context);
			assert(0);
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
	if(m_CurPlayerNum >= m_PlayerNum)
		return -1;
	for(int i=0; i<m_PlayerNum; ++i)
		if(m_Player[i] == NULL)
			return i;
	return -1;
}


bool TractorTable::OnAddGame(Player *player)
{
	if(player->status <= STATUS_AUDIENCE)
	{
		if(m_CurPlayerNum < m_PlayerNum)   //人数未满,成为玩家
		{
			player->status = STATUS_WAIT;
			player->index = GetPlayerIndex();
			assert(player->index != -1);
			m_Player[player->index] = player;
			++m_CurPlayerNum;

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

	ProtocolContext *send_context = NULL;
	send_context = m_GameRoom->NewProtocolContext();
	assert(send_context != NULL);
	send_context->type = DTYPE_BIN;
	send_context->Info = "AddGameRsp";

	KVData send_kvdata(true);
	send_kvdata.SetValue(KEY_Protocol, (int)AddGameRsp);
	string WelcomeMsg="Welcome:"+player->client_name;
	send_kvdata.SetValue(KEY_WelcomeMsg, WelcomeMsg);
	send_kvdata.SetValue(KEY_PlayerNum, m_PlayerNum);
	send_kvdata.SetValue(KEY_ClientNum, m_CurPlayerNum);
	send_kvdata.SetValue(KEY_AudienceNum, (int)m_Audience.size());

	IProtocolFactory *protocol_factory = m_GameRoom->GetProtocolFactory();
	uint32_t header_size = protocol_factory->HeaderSize();
	uint32_t body_size = send_kvdata.Size();
	send_context->CheckSize(header_size+body_size);
	send_kvdata.Serialize(send_context->Buffer+header_size);
	send_context->Size = header_size+body_size;

	//设置NumArray
	int size_array = sizeof(int)*2*(m_CurPlayerNum+m_Audience.size());
	send_context->CheckSize(KVData::SizeBytes(size_array));

	KVBuffer kv_buf = KVData::BeginWrite(send_context->Buffer+send_context->Size, KEY_NumArray, true);
	int *num_array = (int*)kv_buf.second;
	for(int i=0; i<m_PlayerNum; ++i)
	{
		if(m_Player[i] == NULL)
			continue;
		*num_array++ = htonl(m_Player[i]->client_id);
		*num_array++ = htonl((int)m_Player[i]->status);
	}
	for(PPlayerMap::iterator it=m_Audience.begin(); it!=m_Audience.end(); ++it)
	{
		*num_array++ = htonl(it->second->client_id);
		*num_array++ = htonl(it->second->status);
	}

	send_context->Size += KVData::EndWrite(kv_buf, size_array);

	//编译头部
	protocol_factory->EncodeHeader(send_context->Buffer, send_context->Size-header_size);
	if(m_GameRoom->SendProtocol(player->fd, send_context) == false)
	{
		LOG_ERROR(logger, "OnAddGame: send IntoTableRsp to framework failed.fd="<<player->fd);
		m_GameRoom->DeleteProtocolContext(send_context);
		return false;
	}

	LOG_DEBUG(logger, "OnAddGame: send AddGameRsp to framework succ.ClientName="<<player->client_name<<",ClientID="<<player->client_id<<",fd="<<player->fd);
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
		--m_CurPlayerNum;
		//其他玩家设置为STATUS_WAIT状态
		for(int i=0; i<m_PlayerNum; ++i)
		{
			if(m_Player[i] != NULL)
			{
				m_Player[i]->status = STATUS_WAIT;
				m_Player[i]->poker.clear();
			}
		}
	}
	return true;
}

bool TractorTable::OnStartGame(Player *player)
{
	assert(player->table_id==m_TableID && m_Player[player->index]==player && player->status==STATUS_WAIT);
	player->status = STATUS_PLAYING;

	//判断所有的玩家是否都是PLAYING状态
	int i;
	for(i=0; i<m_PlayerNum; ++i)
	{
		if(m_Player[i]==NULL || m_Player[i]->status!=STATUS_PLAYING)
			break;
	}

	//所有玩家都进入PLAYING状态,开始发牌
	if(i >= m_PlayerNum)
	{
		//所有玩家先清牌
		for(i=0; i<m_PlayerNum; ++i)
			m_Player[i]->poker.clear();

		//启动时钟开始发牌
		IEventServer *event_server = m_GameRoom->GetEventServer();
		if(!event_server->AddTimer(this, DEAL_TIMEOUT, false))
		{
			LOG_ERROR(logger, "OnStartGame: all player are ready but add deal timer failed. TableID="<<m_TableID<<". fd="<<player->fd);
			assert(0);
		}
		LOG_INFO(logger, "OnStartGame: all player are ready add deal timer succ. TableID="<<m_TableID<<". fd="<<player->fd);
	}

	return true;
}
