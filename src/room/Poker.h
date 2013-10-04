/*
 * Poker.h
 *
 *  Created on: Sep 29, 2013
 *      Author: tim
 */

#ifndef POKER_H_
#define POKER_H_

#include <assert.h>
#include <vector>
using std::vector;

//牌的表示:
//花色:0XX表示红心,1XX表示黑桃,2XX表示方块,3XX表示草花
//牌面:X02~X14分别表示2~10,J,Q,K,A; 1000:小王;1001大王
class Poker
{
public:
	//使用n副扑克牌
	Poker(int n=1):m_N(n){assert(n>0);}
	//洗牌
	void Shuffle();
	//分牌.返回-1表示没有牌
	int Deal();
	//剩余
	int Remain();
private:
	int m_N;
	vector<int> m_Poker;   //待分发的扑克牌
};

#endif /* TRACTOR_H_ */
