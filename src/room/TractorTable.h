/*
 * TractorTable.h
 *
 *  Created on: Sep 30, 2013
 *      Author: tim
 */

#ifndef TRACTORTABLE_H_
#define TRACTORTABLE_H_

#include <list>
using std::list;

#include "Player.h"
#include "Poker.h"

//拖拉机(80分)
class TractorTable
{
public:
	//n_poker:使用的扑克牌数
	//n_player:玩家个数,至少4个(并且是2的整数倍)
	TractorTable(int n_poker=1, int n_player=4);

	//当前玩家个数
	int CurPlayerNum(){return m_Players.size();}

	//添加一个玩家:true成为玩家;false成为观看者
	bool AddPlayer(Player *player);

	//删除一个玩家:true删除玩家;false删除观看者(或不存在该玩家)
	bool DelPlayer(Player *player);

	//删除所有玩家
	void DelPlayer();

	//给所有玩家发牌
	bool Deal();
private:
	Poker m_Poker;
	int m_PlayerNum;            //玩家个数
	list<Player*> m_Players;    //玩家
	list<Player*> m_Audiences;  //观众
};


#endif /* TRACTORTABLE_H_ */
