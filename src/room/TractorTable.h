/*
 * TractorTable.h
 *
 *  Created on: Sep 30, 2013
 *      Author: tim
 */

#ifndef TRACTORTABLE_H_
#define TRACTORTABLE_H_

#include <stddef.h>
#include <map>
using std::map;

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
	int table_id;  //桌号
	int index;     //玩家时有效,表示第几个玩家
	PlayStatus status;
}Player;
typedef map<int, Player> PlayerMap;

//拖拉机(80分)
class TractorTable
{
public:
	//n_poker:使用的扑克牌数
	TractorTable(int n_poker=1, int n_player=4);
	int GetPlayerIndex();  //获取玩家的index号
	void Shuffle();  //洗牌
	int Deal();      //发牌
public:
	int CurPlayerNum;   //当前玩家个数
	Player *IndexPlayer[10];
	map<int, Player*> Audience;
	int Dealer;         //庄家
private:
	int m_PlayerNum;
	Poker m_Poker;
};

inline
TractorTable::TractorTable(int n_poker/*=1*/, int n_player/*=4*/):m_Poker(n_poker), m_PlayerNum(n_player)
{
	for(int i=0; i<10; ++i)
		IndexPlayer[i] = NULL;
	CurPlayerNum = 0;

	Dealer = -1;
}

inline
int TractorTable::GetPlayerIndex()
{
	if(CurPlayerNum >= m_PlayerNum)
		return -1;
	for(int i=0; i<m_PlayerNum; ++i)
		if(IndexPlayer[i] != NULL)
			return i;
	return -1;
}

inline
void TractorTable::Shuffle()
{
	m_Poker.Shuffle();
}

inline
int TractorTable::Deal()
{
	return m_Poker.Deal();
}

#endif /* TRACTORTABLE_H_ */
