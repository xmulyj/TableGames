/*
 * ClientDeamon.cpp
 *
 *  Created on: Oct 5, 2013
 *      Author: tim
 */
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <assert.h>

#include <string>
#include <vector>
using namespace std;

#include "Socket.h"
#include "KVData.h"
#include "KVDataProtocolFactory.h"
using namespace easynet;

#include "../KeyDefine.h"

typedef enum __client_status__
{
	Status_UnKnow,
	Status_ListAllRoom,
	Status_IntoRoom,
}ClietStatus;

typedef struct _room_item
{
	int RoomID;
	int ClientNum;
	string IP;
	int Port;
}RoomItem;
static vector<RoomItem> gRoomList;

typedef struct _room_info
{
	int RoomID;
	int ClientNum;
	vector<int> NumArray;  //每桌玩家数量
}RoomInfo;

//Game Interface Address
static int    gUID=-1;
static string gUName;
static string gInterfaceIP;
static int    gInterfacePort = 0;
static int    gCurStatus  = Status_ListAllRoom;

static int    gSelectRoomIndex = -2;
static int    gSelectTableIndex = -2;

//刷新所有房间信息
static int RefreshAllRoom();
static int GotoRoom(int room_index);

static void sig_alarm(int)
{
	printf("[alarm...]\n");
}

int main(int argc, char *argv[])
{
	if(argc < 5)
	{
		printf("usage:%s interface_ip interface_port uid uname.\n",argv[0]);
		return -1;
	}

	{
		struct sigaction act, oact;
		act.sa_handler = sig_alarm;
		sigemptyset(&act.sa_mask);
		#ifdef SA_INTERRUPT
		act.sa_flags = SA_INTERRUPT;
		#endif
		if (sigaction(SIGALRM, &act, &oact)<0)
		{
			printf("set alarm signal failed.\n");
			exit(-1);
		}
		//return (oact.sa_handler);
	}


	gInterfaceIP = argv[1];
	gInterfacePort = atoi(argv[2]);
	gUID = atoi(argv[3]);
	gUName = argv[4];

	printf("Set Interface=%s:%d\n", gInterfaceIP.c_str(), gInterfacePort);

	bool finished = false;
	while(!finished)
	{
		switch(gCurStatus)
		{
		case Status_ListAllRoom:  //请求interface
			RefreshAllRoom();
			break;
		case Status_IntoRoom:
			GotoRoom(gSelectRoomIndex);
			break;
		case Status_UnKnow:
		default:
			finished = true;
		}
	}

	printf("\n*** ByeBye:%s ^_^ ***\n", gUName.c_str());
	return 0;
}

////////////////////////////////////////////

static int GetAllRoomInfo()
{
	int fd = Socket::Connect(gInterfacePort, gInterfaceIP.c_str());
	if(fd < 0)
	{
		printf("connect GameInterface=%s:%d failed.\n", gInterfaceIP.c_str(), gInterfacePort);
		exit(-1) ;
	}

	KVDataProtocolFactory factory;
	ProtocolContext context;
	unsigned int header_size, body_size;

	context.type = DTYPE_BIN;
	context.Info = "GetAllRoom";

	KVData kvdata(true);
	kvdata.SetValue(KEY_Protocol, GetAllRoom);
	kvdata.SetValue(KEY_ClientID, gUID);
	kvdata.SetValue(KEY_ClientName, gUName);

	header_size = factory.HeaderSize();
	body_size = kvdata.Size();
	context.CheckSize(header_size+body_size);
	kvdata.Serialize(context.Buffer+header_size);
	context.Size = header_size+body_size;
	factory.EncodeHeader(context.Buffer, body_size);

	int send_size = Socket::SendAll(fd, context.Buffer, context.Size);
	assert(send_size == context.Size);

	//recv resp
	context.header_size = header_size;
	int recv_size = Socket::RecvAll(fd, context.Buffer, context.header_size);
	assert(recv_size == context.header_size);

	if(DECODE_SUCC != factory.DecodeHeader(context.Buffer, context.type, context.body_size))
	{
		printf("decode header failed.");
		Socket::Close(fd);
		return -3;
	}
	recv_size = Socket::RecvAll(fd, context.Buffer+context.header_size, context.body_size);
	assert(recv_size == context.body_size);
	context.Size = context.header_size+context.body_size;

	if(DECODE_SUCC != factory.DecodeBinBody(&context))
	{
		printf("decode body failed.\n");
		Socket::Close(fd);
		return -4;
	}

	KVData *recv_kvdata = (KVData*)context.protocol;
	int Protocol;
	int RoomNum;
	char *NumArray;

	recv_kvdata->GetValue(KEY_Protocol, Protocol);
	assert(Protocol == GetAllRoomRsp);
	recv_kvdata->GetValue(KEY_RoomNum, RoomNum);
	if(RoomNum > 0)
	{
		unsigned int size;
		recv_kvdata->GetValue(KEY_NumArray, NumArray, size);
		assert(size == RoomNum*sizeof(int)*2);
		int *temp_buff = (int*)NumArray;
		for(int i=0; i<RoomNum; ++i)
		{
			RoomItem room_info;
			room_info.RoomID = ntohl(temp_buff[0]);
			room_info.ClientNum = ntohl(temp_buff[1]);
			temp_buff += 2;

			gRoomList.push_back(room_info);
		}
	}
	factory.DeleteProtocol(-1, context.protocol);
	Socket::Close(fd);
}

int RefreshAllRoom()
{
	if(gRoomList.empty())
	{
		GetAllRoomInfo();
	}

	printf("======================\nInterface RoomNum=%d:\n----------------------\n", gRoomList.size());
	for(int i=0; i<gRoomList.size(); ++i)
		printf("[%d]: RoomID=%d, ClientNum=%d\n", i, gRoomList[i].RoomID, gRoomList[i].ClientNum);
	printf("----------------------\n");

	gSelectRoomIndex = -2;
	if(gRoomList.size() > 0)
	{
		alarm(2);
		printf("\nSelect game room index(-1 exit):");
		fflush(stdout);
		scanf("%d", &gSelectRoomIndex);
		if(gSelectRoomIndex == -1)
			gCurStatus = Status_UnKnow;
		else if(gSelectRoomIndex>=0 && gSelectRoomIndex<gRoomList.size())
		{
			printf("go into room[%d] now ...\n", gSelectRoomIndex);
			gCurStatus = Status_IntoRoom;
		}
		alarm(0);
	}

	return 0;
}

int GotoRoom(int room_index)
{
	assert(room_index>=0 && room_index<gRoomList.size());

	int fd = Socket::Connect(gInterfacePort, gInterfaceIP.c_str());
	if(fd < 0)
	{
		printf("connect GameInterface=%s:%d failed.\n", gInterfaceIP.c_str(), gInterfacePort);
		exit(-1) ;
	}

	RoomItem &room_item = gRoomList[room_index];

	//1. get room address
	KVData kvdata(true);
	kvdata.SetValue(KEY_Protocol, GetRoomAddr);
	kvdata.SetValue(KEY_RoomID, room_item.RoomID);
	kvdata.SetValue(KEY_ClientID, gUID);
	kvdata.SetValue(KEY_ClientName, gUName);

	KVDataProtocolFactory factory;
	ProtocolContext context;
	unsigned int header_size, body_size;

	header_size = factory.HeaderSize();
	body_size = kvdata.Size();
	context.CheckSize(header_size+body_size);
	kvdata.Serialize(context.Buffer+header_size);
	context.Size = header_size+body_size;
	factory.EncodeHeader(context.Buffer, body_size);

	int send_size = Socket::SendAll(fd, context.Buffer, context.Size);
	assert(send_size == context.Size);

	//recv resp
	context.header_size = header_size;
	int recv_size = Socket::RecvAll(fd, context.Buffer, context.header_size);
	assert(recv_size == context.header_size);

	if(DECODE_SUCC != factory.DecodeHeader(context.Buffer, context.type, context.body_size))
	{
		printf("decode header failed.");
		Socket::Close(fd);
		return -3;
	}
	recv_size = Socket::RecvAll(fd, context.Buffer+context.header_size, context.body_size);
	Socket::Close(fd);

	assert(recv_size == context.body_size);
	context.Size = context.header_size+context.body_size;

	if(DECODE_SUCC != factory.DecodeBinBody(&context))
	{
		printf("decode body failed.\n");
		Socket::Close(fd);
		return -4;
	}

	KVData *recv_kvdata = (KVData*)context.protocol;
	int Protocol;
	recv_kvdata->GetValue(KEY_Protocol, Protocol);
	assert(Protocol == GetRoomAddrRsp);
	recv_kvdata->GetValue(KEY_RoomIP, room_item.IP);
	recv_kvdata->GetValue(KEY_RoomPort, room_item.Port);

	factory.DeleteProtocol(-1, context.protocol);
	printf("1. get room[%d] address=%s:%d\n", room_index, room_item.IP.c_str(), room_item.Port);

	//2. get room info
	fd = Socket::Connect(room_item.Port, room_item.IP.c_str());
	if(fd < 0)
	{
		printf("connect room=%s:%d failed.\n", room_item.IP.c_str(), room_item.Port);
		gCurStatus = Status_ListAllRoom;
		return -1;
	}
	kvdata.Clear();
	kvdata.SetValue(KEY_Protocol, IntoRoom);
	kvdata.SetValue(KEY_RoomID, room_item.RoomID);
	kvdata.SetValue(KEY_ClientID, gUID);
	kvdata.SetValue(KEY_ClientName, gUName);

	header_size = factory.HeaderSize();
	body_size = kvdata.Size();
	context.CheckSize(header_size+body_size);
	kvdata.Serialize(context.Buffer+header_size);
	context.Size = header_size+body_size;
	factory.EncodeHeader(context.Buffer, body_size);

	send_size = Socket::SendAll(fd, context.Buffer, context.Size);
	assert(send_size == context.Size);

	//recv resp
	context.header_size = header_size;
	recv_size = Socket::RecvAll(fd, context.Buffer, context.header_size);
	assert(recv_size == context.header_size);

	if(DECODE_SUCC != factory.DecodeHeader(context.Buffer, context.type, context.body_size))
	{
		printf("decode header failed.");
		Socket::Close(fd);
		gCurStatus = Status_ListAllRoom;
		return -3;
	}
	recv_size = Socket::RecvAll(fd, context.Buffer+context.header_size, context.body_size);
	Socket::Close(fd);

	assert(recv_size == context.body_size);
	context.Size = context.header_size+context.body_size;

	if(DECODE_SUCC != factory.DecodeBinBody(&context))
	{
		printf("decode body failed.\n");
		Socket::Close(fd);
		gCurStatus = Status_ListAllRoom;
		return -4;
	}


	recv_kvdata = (KVData*)context.protocol;
	recv_kvdata->GetValue(KEY_Protocol, Protocol);
	assert(Protocol == IntoRoomRsp);

	RoomInfo room_info;
	string WelcomeMsg;
	int TableNum;
	unsigned int size;
	char *NumArray;

	recv_kvdata->GetValue(KEY_WelcomeMsg, WelcomeMsg);
	recv_kvdata->GetValue(KEY_RoomID, room_info.RoomID);
	assert(room_info.RoomID == room_item.RoomID);
	recv_kvdata->GetValue(KEY_ClientNum, room_info.ClientNum);
	recv_kvdata->GetValue(KEY_TableNum, TableNum);
	recv_kvdata->GetValue(KEY_NumArray, NumArray, size);
	assert(size == sizeof(int)*TableNum);
	for(int i=0; i<TableNum; ++i)
	{
		room_info.NumArray.push_back(ntohl(*(int*)NumArray));
		NumArray += sizeof(int);
	}
	factory.DeleteProtocol(-1, context.protocol);

	printf("======================\nroom[%d] info: [ClientNum=%d] [TableNum=%d] [ServerMsg:%s]\n----------------------\n", room_index
			,room_info.ClientNum, room_info.NumArray.size(), WelcomeMsg.c_str());
	if(room_info.NumArray.size() > 0)
	{
		int i = 0;
		for(;i<room_info.NumArray.size(); ++i)
		{
			if(i>0 && i%3==0)
				printf("\n");
			printf("[%2d]:PlayerNum=%d\t\t", i, room_info.NumArray[i]);
		}
		if(i%3 != 0)
			printf("\n");
		printf("----------------------\n");


		alarm(3);
		printf("\nselect game table index(-1 back):");
		fflush(stdout);
		gSelectTableIndex = -2;
		scanf("%d", &gSelectTableIndex);
		if(gSelectTableIndex == -1)
			gCurStatus = Status_ListAllRoom;
		else if(gSelectTableIndex>=0 && gSelectTableIndex<room_info.NumArray.size())
			printf("go into room=[%d]->table[%d] now ...\n", gSelectRoomIndex, gSelectTableIndex);
		alarm(0);
	}

	//gCurStatus = Status_UnKnow;
	return 0;
}
