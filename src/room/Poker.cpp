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

//0~12   红心
//13～25 黑桃
//26～38 方块
//39～51 草花
//52～53 小/大王

void Poker::Shuffle()
{
	srand(time(NULL));

	m_Poker.clear();
	int poker[54];
	
	for(int i=0; i<m_N; ++i)
	{
		for(int j=0; j<54; ++j)
			poker[j] = j;
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
