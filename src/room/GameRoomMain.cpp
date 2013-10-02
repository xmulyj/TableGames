/*
 * GameRoomMain.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameRoom.h"

int main(int argc, char *argv[])
{
	INIT_LOGGER("../config/log4cplus.conf");

	GameRoom application;
	application.Start();
	
	return 0;
}

