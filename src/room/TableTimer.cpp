/*
 * TableTimer.cpp
 *
 *  Created on: Oct 3, 2013
 *      Author: tim
 */

#include "TableTimer.h"
#include "GameRoom.h"

#include <assert.h>

//时钟超时
void TableTimer::OnTimeout(uint64_t nowtime_ms)
{
	assert(m_App != NULL);
	GameRoom *game_room = (GameRoom*)m_App;
	if(!game_room->OnTableTimerTimeout(m_TableID))
	{
		//TODO:完成时钟,将时钟移除
	}
}
