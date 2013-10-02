/*
 * GameInterfaceMain.cpp
 *
 *  Created on: 2013-10-01
 *      Author: tim
 */

#include "GameInterface.h"

int main(int argc, char *argv[])
{
	INIT_LOGGER("../config/log4cplus.conf");

	GameInterface application;
	application.Start();
	
	return 0;
}

