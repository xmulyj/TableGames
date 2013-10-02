/*
 * TractorTable.cpp
 *
 *  Created on: Sep 30, 2013
 *      Author: tim
 */

#include "TractorTable.h"
#include <assert.h>
#include <stddef.h>

TractorTable::TractorTable(int n_poker/*=1*/, int n_player/*=4*/):m_Poker(n_poker), m_PlayerNum(n_player)
{
	assert(m_PlayerNum>=4 && m_PlayerNum/2==0);
}

bool TractorTable::AddPlayer(Player *player)
{
	assert(player != NULL);
	if(m_Players.size() < m_PlayerNum)
	{
		m_Players.push_back(player);
		return true;
	}
	m_Audiences.push_back(player);
	return false;
}

bool TractorTable::DelPlayer(Player *player)
{
	list<Player*>::iterator it;
	for(it = m_Players.begin(); it!=m_Players.end(); ++it)
	{
		if(*it == player)
		{
			m_Players.erase(it);
			return true;
		}
	}

	for(it = m_Audiences.begin(); it!=m_Audiences.end(); ++it)
	{
		if(*it == player)
			m_Audiences.erase(it);
	}
	return false;
}

void TractorTable::DelPlayer()
{
	m_Players.clear();
	m_Audiences.clear();
}

