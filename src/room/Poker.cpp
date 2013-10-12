/*
 * Tractor.cpp
 *
 *  Created on: Sep 29, 2013
 *      Author: tim
 */

#include "Poker.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>

static int g_Poker[54] =
{
	2,3,4,5,6,7,8,9,10,11,12,13,14,                        /*红心*/
	102,103,104,105,106,107,108,109,110,111,112,113,114,   /*黑桃*/
	202,203,204,205,206,207,208,209,210,211,212,213,214,   /*方块*/
	302,303,304,305,306,307,308,309,310,311,312,313,314,   /*草花*/
	1000,10001                                             /*小王,大王*/
};

void Poker::Shuffle()
{
	srand(time(NULL));

	m_Poker.clear();
	int poker[54];
	for(int i=0; i<m_N; ++i)
	{
		memcpy((void*)poker, (void*)g_Poker, sizeof(int)*54);
		for(int j=54; j>0; --j)
		{
			int k = rand()%j;
			m_Poker.push_back(poker[k]);
			poker[k] = poker[j-1];
		}
	}
}

int Poker::Deal()
{
	int poker = -1;
	if(m_Poker.size() > 0)
	{
		poker = m_Poker.back();
		m_Poker.pop_back();
	}
	return poker;
}

int Poker::Remain()
{
	return m_Poker.size();
}
