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
	Status_SelectTable,
}ClietStatus;

typedef struct _room_info
{
	int         RoomID;
	int         ClientNum;
	string      IP;
	int         Port;

	int         fd;  //连接
	vector<int> TableArray;  //每桌玩家数量
}RoomInfo;
static vector<RoomInfo> gRoomList;


//Game Interface Address
static int    gUID=-1;
static string gUName;
static string gInterfaceIP;
static int    gInterfacePort = 0;
static int    gCurStatus  = Status_ListAllRoom;

static int    gSelectRoomIndex = -2;
static int    gSelectTableIndex = -2;

//刷新所有房间信息
static int _GetAllRoomInfo();
static int OnRefreshAllRoom();

static bool _GetRoomAddr(RoomInfo &room_Info);
static int OnGotoRoom(int room_index);

static int OnSelectTable(int room_index, int table_index);

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
			OnRefreshAllRoom();
			break;
		case Status_IntoRoom:
			OnGotoRoom(gSelectRoomIndex);
			break;
		case Status_SelectTable:
			OnSelectTable(gSelectRoomIndex, gSelectTableIndex);
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

int _GetAllRoomInfo()
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

	context.CheckSize(context.header_size+context.body_size);
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
			RoomInfo room_info;
			room_info.RoomID = ntohl(temp_buff[0]);
			room_info.ClientNum = ntohl(temp_buff[1]);
			room_info.fd = -1;
			temp_buff += 2;
			gRoomList.push_back(room_info);
		}
	}
	factory.DeleteProtocol(-1, context.protocol);
	Socket::Close(fd);
}

int OnRefreshAllRoom()
{
	if(gRoomList.empty())
	{
		_GetAllRoomInfo();
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
		else
		{
			gRoomList.clear();  //清空,下次刷新重新获取所有房间列表
		}
		alarm(0);
	}

	return 0;
}

bool _GetRoomAddr(RoomInfo &room_info)
{
	int fd = Socket::Connect(gInterfacePort, gInterfaceIP.c_str());
	if(fd < 0)
	{
		printf("connect GameInterface=%s:%d failed.\n", gInterfaceIP.c_str(), gInterfacePort);
		return false;
	}

	//1. get room address
	KVData kvdata(true);
	kvdata.SetValue(KEY_Protocol, GetRoomAddr);
	kvdata.SetValue(KEY_RoomID, room_info.RoomID);
	kvdata.SetValue(KEY_ClientID, gUID);
	kvdata.SetValue(KEY_ClientName, gUName);

	KVDataProtocolFactory factory;
	unsigned int header_size = factory.HeaderSize();
	unsigned int body_size = kvdata.Size();
	ProtocolContext context(header_size+body_size);

	kvdata.Serialize(context.Buffer+header_size);
	context.Size = header_size+body_size;
	factory.EncodeHeader(context.Buffer, body_size);

	int send_size = Socket::SendAll(fd, context.Buffer, context.Size);
	assert(send_size == context.Size);

	//receive respond
	context.header_size = header_size;
	int recv_size = Socket::RecvAll(fd, context.Buffer, context.header_size);
	assert(recv_size == context.header_size);

	if(DECODE_SUCC != factory.DecodeHeader(context.Buffer, context.type, context.body_size))
	{
		printf("decode header failed.");
		Socket::Close(fd);
		return false;
	}

	context.CheckSize(context.header_size+context.body_size);
	recv_size = Socket::RecvAll(fd, context.Buffer+context.header_size, context.body_size);
	assert(recv_size == context.body_size);
	context.Size = context.header_size+context.body_size;

	Socket::Close(fd);
	if(DECODE_SUCC != factory.DecodeBinBody(&context))
	{
		printf("decode body failed.\n");
		return false;
	}

	KVData *recv_kvdata = (KVData*)context.protocol;
	int Protocol;
	recv_kvdata->GetValue(KEY_Protocol, Protocol);
	assert(Protocol == GetRoomAddrRsp);

	recv_kvdata->GetValue(KEY_RoomIP, room_info.IP);
	recv_kvdata->GetValue(KEY_RoomPort, room_info.Port);

	factory.DeleteProtocol(-1, context.protocol);
	return true;
}

int OnGotoRoom(int room_index)
{
	assert(room_index>=0 && room_index<gRoomList.size());
	RoomInfo &room_info = gRoomList[room_index];
	if(room_info.fd < 0)
	{
		if(!_GetRoomAddr(room_info))
		{
			printf("get room[%d] address failed.\n");
			gCurStatus = Status_ListAllRoom;
			return -1;
		}
		printf("get room[%d] address successful. ip=%s:%d\n", room_index, gRoomList[room_index].IP.c_str(), gRoomList[room_index].Port);
		//connect room
		int fd = Socket::Connect(room_info.Port, room_info.IP.c_str());
		if(fd < 0)
		{
			printf("connect room=%s:%d failed.\n", room_info.IP.c_str(), room_info.Port);
			gCurStatus = Status_ListAllRoom;
			return -2;
		}
		room_info.fd = fd;
	}

	printf("get room info...\n");
	//get room info;
	KVData kvdata(true);
	kvdata.SetValue(KEY_Protocol, GetRoomInfo);
	kvdata.SetValue(KEY_RoomID, room_info.RoomID);
	kvdata.SetValue(KEY_ClientID, gUID);
	kvdata.SetValue(KEY_ClientName, gUName);

	KVDataProtocolFactory factory;
	unsigned int header_size = factory.HeaderSize();
	unsigned int body_size = kvdata.Size();
	ProtocolContext context(header_size+body_size);

	kvdata.Serialize(context.Buffer+header_size);
	context.Size = header_size+body_size;
	factory.EncodeHeader(context.Buffer, body_size);

	int send_size = Socket::SendAll(room_info.fd, context.Buffer, context.Size);
	assert(send_size == context.Size);

	//receive respond
	context.header_size = header_size;
	int recv_size = Socket::RecvAll(room_info.fd, context.Buffer, context.header_size);
	assert(recv_size == context.header_size);

	if(DECODE_SUCC != factory.DecodeHeader(context.Buffer, context.type, context.body_size))
	{
		printf("decode header failed.");
		Socket::Close(room_info.fd);
		room_info.fd = -1;
		gCurStatus = Status_ListAllRoom;
		return -3;
	}

	context.CheckSize(context.header_size+context.body_size);
	recv_size = Socket::RecvAll(room_info.fd, context.Buffer+context.header_size, context.body_size);
	assert(recv_size == context.body_size);
	context.Size = context.header_size+context.body_size;

	if(DECODE_SUCC != factory.DecodeBinBody(&context))
	{
		printf("decode body failed.\n");
		Socket::Close(room_info.fd);
		room_info.fd = -1;
		gCurStatus = Status_ListAllRoom;
		return -4;
	}

	KVData *recv_kvdata = (KVData*)context.protocol;
	int Protocol;
	recv_kvdata->GetValue(KEY_Protocol, Protocol);
	assert(Protocol == GetRoomInfoRsp);

	int RoomID;
	int TableNum;
	unsigned int size;
	char *NumArray;

	recv_kvdata->GetValue(KEY_RoomID, RoomID);
	assert(room_info.RoomID == RoomID);
	recv_kvdata->GetValue(KEY_ClientNum, room_info.ClientNum);
	recv_kvdata->GetValue(KEY_TableNum, TableNum);
	recv_kvdata->GetValue(KEY_NumArray, NumArray, size);
	assert(size == sizeof(int)*TableNum);
	for(int i=0; i<TableNum; ++i)
	{
		room_info.TableArray.push_back(ntohl(*(int*)NumArray));
		NumArray += sizeof(int);
	}
	factory.DeleteProtocol(-1, context.protocol);

	printf("======================\nroom[%d] info: [ClientNum=%d] [TableNum=%d]\n----------------------\n", room_index
			,room_info.ClientNum, room_info.TableArray.size());
	if(room_info.TableArray.size() > 0)
	{
		int i = 0;
		for(;i<room_info.TableArray.size(); ++i)
		{
			if(i>0 && i%3==0)
				printf("\n");
			printf("[%2d]:PlayerNum=%d\t\t", i, room_info.TableArray[i]);
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
		{
			Socket::Close(room_info.fd);
			room_info.fd = -1;
			gCurStatus = Status_ListAllRoom;
		}
		else if(gSelectTableIndex>=0 && gSelectTableIndex<room_info.TableArray.size())
		{
			printf("go into room=[%d]->table[%d] now ...\n", gSelectRoomIndex, gSelectTableIndex);
			gCurStatus = Status_SelectTable;
		}
		else
		{
			room_info.TableArray.clear();  //清空,下次刷新重新获取
		}
		alarm(0);
	}

	return 0;
}

int OnSelectTable(int room_index, int table_index)
{
	RoomInfo &room_info = gRoomList[room_index];
	if(room_info.fd < 0)
	{
		if(!_GetRoomAddr(room_info))
		{
			printf("get room[%d] address failed.\n");
			gCurStatus = Status_IntoRoom;
			return -1;
		}
		printf("get room[%d] address successful. ip=%s:%d\n", room_index, gRoomList[room_index].IP.c_str(), gRoomList[room_index].Port);
		//connect room
		int fd = Socket::Connect(room_info.Port, room_info.IP.c_str());
		if(fd < 0)
		{
			printf("connect room=%s:%d failed.\n", room_info.IP.c_str(), room_info.Port);
			gCurStatus = Status_IntoRoom;
			return -2;
		}
		room_info.fd = fd;
	}

	printf("add game. room=[%d]->table[%d] ...\n", room_index, table_index);
	//get room info;
	KVData kvdata(true);
	kvdata.SetValue(KEY_Protocol, AddGame);
	kvdata.SetValue(KEY_RoomID, room_info.RoomID);
	kvdata.SetValue(KEY_TableID, table_index);
	kvdata.SetValue(KEY_ClientID, gUID);
	kvdata.SetValue(KEY_ClientName, gUName);

	KVDataProtocolFactory factory;
	unsigned int header_size = factory.HeaderSize();
	unsigned int body_size = kvdata.Size();
	ProtocolContext context(header_size+body_size);

	kvdata.Serialize(context.Buffer+header_size);
	context.Size = header_size+body_size;
	factory.EncodeHeader(context.Buffer, body_size);

	int send_size = Socket::SendAll(room_info.fd, context.Buffer, context.Size);
	assert(send_size == context.Size);

	//receive respond
	context.header_size = header_size;
	int recv_size = Socket::RecvAll(room_info.fd, context.Buffer, context.header_size);
	assert(recv_size == context.header_size);

	if(DECODE_SUCC != factory.DecodeHeader(context.Buffer, context.type, context.body_size))
	{
		printf("decode header failed.");
		Socket::Close(room_info.fd);
		room_info.fd = -1;
		gCurStatus = Status_IntoRoom;
		return -3;
	}

	context.CheckSize(context.header_size+context.body_size);
	recv_size = Socket::RecvAll(room_info.fd, context.Buffer+context.header_size, context.body_size);
	assert(recv_size == context.body_size);
	context.Size = context.header_size+context.body_size;

	if(DECODE_SUCC != factory.DecodeBinBody(&context))
	{
		printf("decode body failed.\n");
		Socket::Close(room_info.fd);
		room_info.fd = -1;
		gCurStatus = Status_IntoRoom;
		return -4;
	}

	KVData *recv_kvdata = (KVData*)context.protocol;
	int Protocol;
	recv_kvdata->GetValue(KEY_Protocol, Protocol);
	assert(Protocol == AddGameRsp);

	string WelcomeMsg;
	int    Status;
	recv_kvdata->GetValue(KEY_WelcomeMsg, WelcomeMsg);
	recv_kvdata->GetValue(KEY_Status, Status);
	factory.DeleteProtocol(-1, context.protocol);

	string StrStatus;
	if(Status == 1)
		StrStatus = "Audience";
	else if(Status == 2)
		StrStatus = "Wait to Start";
	else if(Status == 3)
		StrStatus = "Playing";
	else
		StrStatus = "UnkonwStatus!!!";

	alarm(3);
	if(Status != 2)
	{
		printf("ServerMsg=[%s],Status=[%s]\n", WelcomeMsg.c_str(), StrStatus.c_str());
		pause();
	}
	else
	{
		alarm(3);
		printf("ServerMsg=[%s],Status=[%s]\nStart?(y/n):", WelcomeMsg.c_str(), StrStatus.c_str());
		char c = 0;
		scanf("%c", &c);
		if(c=='y' || c=='Y')
		{
			//get room info;
			KVData kvdata(true);
			kvdata.SetValue(KEY_Protocol, StartGame);
			kvdata.SetValue(KEY_RoomID, room_info.RoomID);
			kvdata.SetValue(KEY_TableID, table_index);
			kvdata.SetValue(KEY_ClientID, gUID);
			kvdata.SetValue(KEY_ClientName, gUName);

			KVDataProtocolFactory factory;
			unsigned int header_size = factory.HeaderSize();
			unsigned int body_size = kvdata.Size();
			ProtocolContext context(header_size+body_size);

			kvdata.Serialize(context.Buffer+header_size);
			context.Size = header_size+body_size;
			factory.EncodeHeader(context.Buffer, body_size);

			int send_size = Socket::SendAll(room_info.fd, context.Buffer, context.Size);
			assert(send_size == context.Size);
		}
	}
	alarm(0);

	return 0;
}
