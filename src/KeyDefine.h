/*
 * KeyDefine.h
 *
 *  Created on: Oct 1, 2013
 *      Author: tim
 */

#ifndef KEYDEFINE_H_
#define KEYDEFINE_H_

//Protocol Type
//////////////////////////////
#define ReportRoomInfo      0
#define GetAllRoom          1
#define GetAllRoomRsp       2
#define GetRoomAddr         3
#define GetRoomAddrRsp      4
#define GetRoomInfo         7
#define GetRoomInfoRsp      8
#define AddGame             9
#define AddGameRsp          10
#define QuitGame            11
#define StartGame           12
#define DealPoker           13

//KeyName Define
//////////////////////////////
#define KEY_Protocol        0    //value:见Protocol Type
#define KEY_RoomID          1
#define KEY_RoomIP          2
#define KEY_RoomPort        3
#define KEY_ClientNum       4
#define KEY_TableNum        5
#define KEY_TableID         6
#define KEY_NumArray        7
#define KEY_ClientID        8
#define KEY_ClientName      9
#define KEY_RoomNum         10
#define KEY_WelcomeMsg      11
#define KEY_Status          12


#endif /* KEYDEFINE_H_ */
