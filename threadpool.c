#include"threadpool.h"

void initThreadPool(threadPool *pool ,int _thread_num, int _max_task_num){
	pool->max_task_num = _max_task_num;
	pool->task_num = 0;
	pool->thread_num = _thread_num;
	//创建任务队列
	pool->taskqueue = (threadTask*)malloc(pool->max_task_num * sizeof(threadTask));
	pool->qhead = 0;
	pool->qreal = 0;
	pool->shutdown = 0;
	//创建线程数组
	pool->threads = (pthread_t *)malloc(pool->thread_num * sizeof(pthread_t));
	
	//创建线程
	int i = 0;
	for(i;i<pool->thread_num;i++){
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);//设置分离
		pthread_create(&(pool->threads[i]), &attr,threadRun, (void*)pool);
		
		#if MSG
		printf("thread [%ld] create success!\n", pool->threads[i]);
		#endif

		pthread_attr_destroy(&attr);
	}

	//初始化互斥锁
	pthread_mutex_init(&(pool->pool_lock), NULL);

	//初始化条件变量
	pthread_cond_init(&(pool->not_empty_task), NULL);
	pthread_cond_init(&(pool->not_fill_task), NULL);


}

void* threadRun(void *arg){
	threadPool *pool = (threadPool *)arg;
	
	//每一个线程执行的函数
	while(1){

		pthread_mutex_lock(&(pool->pool_lock));//上锁
				
		if(pool->shutdown == 1){
		//线程池销毁
			#if MSG
			printf("thread [%ld] exit...\n",pthread_self());
			#endif
			pthread_mutex_unlock(&(pool->pool_lock)); 
			pthread_exit( NULL);
		}
		//申请任务队列中的任务
		
		//如果任务队列为空，就阻塞等待
		while(pool->task_num == 0){
			pthread_cond_wait(&(pool->not_empty_task), &(pool->pool_lock));
		}

		if(pool->task_num == 0){

			pthread_cond_signal(&(pool->not_empty_task));
			pthread_mutex_unlock(&(pool->pool_lock));//解锁
			return NULL;
		}

		#if MSG
		printf("start deal task...\n");
		#endif

		//准备开始进行任务
		//先判断线程池状态
		if(pool->shutdown == 1){
			//开始结束线程
			#if MSG
			printf("thread [%ld] exit...\n",pthread_self());
			#endif
			pthread_exit(NULL);
		}

		#if MSG
			printf("thread[%ld] :获取任务任务[%d]......\n", pthread_self(), 
				pool->taskqueue[pool->qhead].taskid);
		#endif

		threadTask* task = (threadTask*)malloc(sizeof(threadTask));
		task->arg = pool->taskqueue[pool->qhead].arg;
		task->callback = pool->taskqueue[pool->qhead].callback;
		task->taskid = pool->taskqueue[pool->qhead].taskid;

		pool->qhead = (pool->qhead+1)%(pool->max_task_num);
		(pool->task_num)--;

		if(pool->task_num>0){
			pthread_cond_signal(&(pool->not_empty_task));
		}

		//唤醒因为任务队列满了而阻塞的线程
		pthread_cond_signal(&(pool->not_fill_task));
		pthread_mutex_unlock(&(pool->pool_lock));//解锁

		task->callback(task->arg);

		#if MSG
		printf("thread[%ld] :完成任务[%d]! \n", pthread_self(), 
				task->taskid);
		#endif
		free(task);		
	}

}

void addTask(threadPool *pool, threadTask* task){
	//这个是主线程才会执行

	pthread_mutex_lock(&(pool->pool_lock)); //上锁
	//任务队列如果满了，就阻塞等待
	while(pool->task_num  == pool->max_task_num){
		pthread_cond_wait(&(pool->not_fill_task), &(pool->pool_lock));
	}
	if(pool->shutdown == 1){
		//线程池销毁
		pthread_cond_signal(&(pool->not_empty_task));
		pthread_mutex_unlock(&(pool->pool_lock)); 
		return ;
	}
		
	//加入到任务队列
	pool->taskqueue[pool->qreal].taskid = task->taskid;
	pool->taskqueue[pool->qreal].arg = task->arg;
	pool->taskqueue[pool->qreal].callback = task->callback;
	
	pool->qreal = (pool->qreal+1)%(pool->max_task_num);

	(pool->task_num)++;

	#if MSG
	printf("任务[%d]添加成功! 现有任务数: %d\n", task->taskid, pool->task_num);
	#endif

	int ret = pthread_cond_signal(&(pool->not_empty_task));
	if(0 != ret){
	
		printf("signal error\n");
	}

	ret = pthread_mutex_unlock(&(pool->pool_lock)); //解锁
	if(0 != ret){
		printf("unlock error!!!!!!!!!!\n");
	}

}

void destroyThreadPool(threadPool *pool){
	if(pool->shutdown == 1)
		return ;
	pool->shutdown = 1;
	//通知所有线程让他们自杀
	pthread_cond_broadcast(&(pool->not_empty_task));

	//销毁互斥量与条件变量
	pthread_mutex_destroy(&(pool->pool_lock));
	pthread_cond_destroy(&(pool->not_empty_task));
	pthread_cond_destroy(&(pool->not_fill_task));

	//释放任务队列
	free(pool->taskqueue);	
	
	//释放线程池数组
	free(pool->threads);
}

