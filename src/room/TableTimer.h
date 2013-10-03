/*
 * TableTimer.h
 *
 *  Created on: Oct 3, 2013
 *      Author: tim
 */

#ifndef TABLETIMER_H_
#define TABLETIMER_H_


#include "IAppInterface.h"
#include "IEventHandler.h"
using namespace easynet;

class TableTimer:public IEventHandler
{
public:
	void Init(IAppInterface *app, int table_id):m_App(app),m_TableID(table_id){;}
	//时钟超时
	void OnTimeout(uint64_t nowtime_ms);
	//错误事件
	void OnEventError(int32_t fd, uint64_t nowtime_ms, ERROR_CODE code){};
	//可读事件
	ERROR_CODE OnEventRead(int32_t fd, uint64_t nowtime_ms){return ECODE_SUCC;}
	//可写事件
	ERROR_CODE OnEventWrite(int32_t fd, uint64_t nowtime_ms){return ECODE_SUCC;}
public:
	int m_TableID;
	IAppInterface *m_App;
};

#endif /* TABLETIMER_H_ */
