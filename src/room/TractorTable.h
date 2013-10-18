/*
 * TractorTable.h
 *
 *  Created on: Sep 30, 2013
 *      Author: tim
 */

#ifndef TRACTORTABLE_H_
#define TRACTORTABLE_H_

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <vector>
#include <string>
using std::map;
using std::vector;
using std::string;

#include "EventServer.h"
using namespace easynet;

#include "Logger.h"

#include "Poker.h"


typedef enum _status_
{
	STATUS_INVALID=0,     //无效
	STATUS_AUDIENCE=1,    //观众
	STATUS_WAIT=2,        //玩家,等待开始
	STATUS_PLAYING=3,     //玩家,开始游戏
}PlayStatus;

typedef struct _player_
{
	int client_id;
	string client_name;
	int table_id;  //桌号
	int index;     //玩家时有效,表示第几个玩家
	PlayStatus status;
	vector<int> poker;  //拿到的牌
	int fd;
}Player;
typedef map<int, Player>  PlayerMap;
typedef map<int, Player*> PPlayerMap;

class GameRoom;

//拖拉机游戏
class TractorTable:public IEventHandler
{
public:
	//时钟超时
	void OnTimeout(uint64_t nowtime_ms);
	//错误事件
	void OnEventError(int32_t fd, uint64_t nowtime_ms, ERROR_CODE code){};
	//可读事件
	ERROR_CODE OnEventRead(int32_t fd, uint64_t nowtime_ms){return ECODE_SUCC;}
	//可写事件
	ERROR_CODE OnEventWrite(int32_t fd, uint64_t nowtime_ms){return ECODE_SUCC;}
public:
	//n_poker:使用的扑克牌数
	TractorTable(GameRoom *game_room, int table_id, int n_poker=1, int n_player=4);

	bool OnAddGame(Player *player);
	bool OnQuitGame(Player *player);
	bool OnStartGame(Player *player);

	int CurPlayerNum(){return m_PlayerNum;}

	//玩家列表:用0,1,2,3位表示,该位为0表示没有玩家,1表示有玩家
	int GetPlayerArray()
	{
		int bitmap = 0;
		for(int i=0; i<m_PlayerNum; ++i)
			if(m_Player[i] != NULL)
				bitmap |= (1<<i);
		return bitmap;
	}
private:
	int GetPlayerIndex();  //获取玩家的index号
	void SendAddGameRsp(int fd, string &msg);  //广播有用户进入游戏桌子
	void AddGameBroadCast(Player *player);     //向桌内其他玩家广播消息
	void QuitGameBroadCast(Player *player);   //向桌内其他玩家广播消息
	void StartGameBroadCast(Player *player);   //向桌内其他玩家广播消息
private:
	GameRoom*            m_GameRoom;       //游戏房间
	int                  m_TableID;        //桌号
	int                  m_NeedNum;        //游戏所需玩家个数

	Poker                m_Poker;          //扑克
	int                  m_PlayerNum;      //当前玩家个数
	Player*              m_Player[10];     //玩家
	PPlayerMap           m_Audience;       //旁观者
	int                  m_Dealer;         //庄家的号码
	int                  m_KeepPokerNum;   //底牌张数
	int                  m_CulLevel;       //当前级别
	int                  m_PokerNum;       //当前还剩扑克牌数量

	//(0~3bit)当前主花色:
	//-1:未设置;0:红心;1:黑桃;2:方块:3:草花;4小王;5大王
	//(4~bit)当前花色数量;
	int                  m_CulColor;
	vector<int>          m_Score;          //当前分数

private:
	DECL_LOGGER(logger);
};

#endif /* TRACTORTABLE_H_ */
