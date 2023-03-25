#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<unistd.h>
#include<pthread.h>
#include<sys/epoll.h>

#define MSG 1  //设置为1可以打印任务提交任务处理等信息

typedef struct _threadTask{
	int taskid; //任务号
	void *arg; //回调函数参数
	void (* callback)(void *arg_); //回调函数
	
}threadTask;

typedef struct _threadPool{
	int max_task_num; //最大任务数
	int task_num; //当前任务数
	threadTask *taskqueue; //任务队列(用循环数组)
	int qhead; //队头位置(要出队的位置)
	int qreal; //队尾位置(要入队的位置)
	
	int thread_num; //线程池线程数
	int shutdown; //是否关闭线程池 1 表示关闭
	pthread_t *threads; //线程池线程数组
	pthread_mutex_t pool_lock; //线程池锁
	pthread_cond_t not_empty_task; //任务队列不是空
	pthread_cond_t not_fill_task; //任务队列未满

}threadPool;

void initThreadPool(threadPool *pool, int _thread_num, int _max_task_num ); //初始化>    线程池
void* threadRun(void *arg); //线程调用函数_取任务函数任务
void addTask(threadPool* pool,threadTask *task); //添加任务
void destroyThreadPool(threadPool* pool); //销毁线程池


#endif
