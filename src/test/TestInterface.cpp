/*
 * TestInterface.cpp
 *
 *  Created on: Oct 5, 2013
 *      Author: tim
 */
#include <stdlib.h>
#include <netinet/in.h>

#include "../KeyDefine.h"
#include "KVData.h"
#include "KVDataProtocolFactory.h"

#include <string>
#include <vector>
using std::string;
using std::vector;

#include "Socket.h"
using namespace easynet;

typedef struct _room_info
{
	int RoomID;
	int ClientNum;
	string IP;
	int Port;
}RoomInfo;

int main(int argc, char *argv[])
{
	if(argc < 5)
	{
		printf("usage: %s ip port uid uname\n", argv[0]);
		return -1;
	}

	char *ip = argv[1];
	int port = atoi(argv[2]);
	int uid  = atoi(argv[3]);
	string uname = argv[4];

	printf("param info:interface=%s:%d,uid=%d,uname=%s\n", ip,port,uid,uname.c_str());

	int fd = Socket::Connect(port, ip, true, -1);
	if(fd < 0)
	{
		printf("connect server failed.\n");
		return -2;
	}

	KVDataProtocolFactory factory;
	ProtocolContext context;
	unsigned int header_size, body_size;

	context.type = DTYPE_BIN;
	context.Info = "GetAllRoom";

	KVData kvdata(true);
	kvdata.SetValue(KEY_Protocol, GetAllRoom);
	kvdata.SetValue(KEY_ClientID, uid);
	kvdata.SetValue(KEY_ClientName, uname);

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
	vector<RoomInfo> AllRoomInfo;
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
			temp_buff += 2;

			AllRoomInfo.push_back(room_info);
		}
	}
	factory.DeleteProtocol(-1, context.protocol);

	//Get RoomAddr
	for(int i=0; i<AllRoomInfo.size(); ++i)
	{
		//Get RoomAddr
		kvdata.Clear();
		kvdata.SetValue(KEY_Protocol, GetRoomAddr);
		kvdata.SetValue(KEY_RoomID, AllRoomInfo[i].RoomID);
		kvdata.SetValue(KEY_ClientID, uid);
		kvdata.SetValue(KEY_ClientName, uname);

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
		recv_kvdata->GetValue(KEY_Protocol, Protocol);
		assert(Protocol == GetRoomAddrRsp);
		recv_kvdata->GetValue(KEY_RoomIP, AllRoomInfo[i].IP);
		recv_kvdata->GetValue(KEY_RoomPort, AllRoomInfo[i].Port);

		factory.DeleteProtocol(-1, context.protocol);
	}

	printf("Interface RoomNum=%d:\n", RoomNum);
	for(int i=0; i<AllRoomInfo.size(); ++i)
		printf("\t%d: RoomID=%d, ClientNum=%d, IP=%s:%d\n", i
				,AllRoomInfo[i].RoomID
				,AllRoomInfo[i].ClientNum
				,AllRoomInfo[i].IP.c_str()
				,AllRoomInfo[i].Port);

	Socket::Close(fd);
	return 0;
}


