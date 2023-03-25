#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "threadpool.h"
#include <sys/signal.h>

#define THREADS_NUM 3 //线程数量
#define TASK_QUEUE_SIZE 256 //任务队列大小
#define EPOLL_EVENT_ARRAY_SIZE 10240 //epoll_event数组上限

// int num_c = 0;

struct cfd_and_efd{
	int cfd;
	int efd;
};
// 读取客户端请求
void read_client_request(int efd, int cfd);

// 注册事件
void addFd(int efd, int cfd, int isOneShot);

// 删除事件
void delFd(int efd, int cfd);

// 设置非阻塞
int setnonblocking(int fd);

// 更改EPOLLONESHOT
void reset_OneShot(int efd, int cfd);

// 根据fd打印客户端状态
void printCilentStatus(int fd, const char *status);

// 发送http 头部包
void send_http_head(int cfd, int code, char *info, char *conenttype);

// 线程处理客户端请求的回调
void threadwork_cb(void *arg);

// 处理contenttype
char *get_mime_type(char *name);

// 发送文件数据
void send_file(int cfd, char *path, int efd, int isclose);

// 回复客户端的请求
void send_http_data(int cfd, char *path, int efd);

// 使用epoll实现http服务器
int main(int argc, char *args[])
{
	if (argc < 3){
		printf("请设置ip和端口号!\n");
		printf("例如： %s 127.0.0.1 8888\n", args[0]);
		return 1;
	}

	signal(SIGPIPE, SIG_IGN);

	// 获取当前路径并设置为工作目录
	char *workdir = getenv("PWD");
	workdir = strcat(workdir, "/data");
	chdir(workdir);

	// 给权限
	umask(0);

	// 创建套接字
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == lfd){
		perror("socket");
		return 1;
	}
	// 绑定 bind
	struct sockaddr_in addr;
	socklen_t t1 = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(args[2]));
	inet_pton(AF_INET, args[1], &addr.sin_addr.s_addr);
	int ret = bind(lfd, (struct sockaddr *)&addr, t1);
	if (-1 == ret){
		printf("bind failed\n");
		close(lfd);
		return 1;
	}
	// 监听
	listen(lfd, 128);
	// 创建树
	int efd = epoll_create(1);
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;

	ret = epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &ev);
	if (-1 == ret){
		perror("epoll add failed\n");
		close(efd);
		return 1;
	}

	// 创建线程池
	threadPool pool;

	// 线程数  任务队列大小
	initThreadPool(&pool, THREADS_NUM, TASK_QUEUE_SIZE);

	// 循环监听
	struct epoll_event active_events[EPOLL_EVENT_ARRAY_SIZE];
	int nready;

	struct sockaddr_in ad;
	socklen_t t2;
	int cfd;

	while (1){

		nready = epoll_wait(efd, active_events, EPOLL_EVENT_ARRAY_SIZE, -1);
		if (nready < 0){
			perror("epoll wait failed\n");
			continue;
		}
		else{
			for (int i = 0; i < nready; i++){

				if (active_events[i].data.fd == lfd){
					// 表示有新的连接建立
					cfd = accept(lfd, (struct sockaddr *)&ad, &t2);
					if (-1 == cfd){
						perror("accept");
						break;
					}
					//设置非阻塞模式
					setnonblocking(cfd);

					printCilentStatus(cfd, "login");
					// 将cfd注册
					addFd(efd, cfd, 1);
				}
				else if (active_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){

					// 删除fd事件
					delFd(efd, active_events[i].data.fd);

				}
				else if (active_events[i].events & EPOLLIN){
					// 客户端发送数据来了
					// 创建任务
					threadTask task;
					// task.taskid = num_c++;
					task.taskid = 0; //这个不重要
					task.callback = threadwork_cb;

					struct cfd_and_efd *p = (struct cfd_and_efd *)malloc(sizeof(struct cfd_and_efd));
					p->cfd = active_events[i].data.fd;
					p->efd = efd;
					task.arg = (void *)p;

					//添加任务
					addTask(&pool, &task);
				}
				else{
					// printf("特殊监听.....\n");
				}
			}
		}
	}
	// 收尾

	close(lfd);
	pool.shutdown = 1;
	destroyThreadPool(&pool);
	printf("webServer will exit after 2s \n");
	sleep(2);
	return 0;
}

// 读取客户端请求
void read_client_request(int efd, int cfd){
	char buf[2048];
	memset(buf, 0, 2048);
	// 非阻塞模式
	// 立即返回
	int ret;
	while (1){
		memset(buf, 0, 2048);
		ret = recv(cfd, buf, 2048, 0);
		if (0 > ret){
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				reset_OneShot(efd, cfd);
				return;
			}
			perror("recv");
			return;
		}
		else if (0 == ret){
			// 客户端关闭
			printCilentStatus(cfd, " exit");
			// 下树
			delFd(efd, cfd);
			return;
		}
		else{
			//表示读取到数据
			break;
		}
	}

	// 客户端向服务器发送请求
	char method[256] = "";
	char content[256] = "";
	char protocol[256] = "";
	sscanf(buf, "%[^ ] %[^ ] %[^ \r\n]", method, content, protocol);

	int i = strlen(content);
	for (i; i >= 0; i--){
		content[i + 1] = content[i];
	}
	content[0] = '.';
	printf("======[%s]  [%s]  [%s]======\n", method, content, protocol);

	if (0 == strcmp(method, "GET")){
		// GET请求
		send_http_data(cfd, content, efd);
	}
	else{

	}
	//reset_OneShot(efd, cfd);

}

// 注册事件
void addFd(int efd, int cfd, int isOneShot){
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	ev.data.fd = cfd;
	if (isOneShot){
		ev.events |= EPOLLONESHOT;
	}

	int ret = epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &ev);
	if (-1 == ret){
		perror("epoll_ctl add");
		return;
	}
}

// 删除事件
void delFd(int efd, int cfd){
	// 打印一下客户端下线信息
	printCilentStatus(cfd, " exit!");

	int ret = epoll_ctl(efd, EPOLL_CTL_DEL, cfd, NULL);
	if (-1 == ret){
		perror("epoll ctl del failed");
	}
	close(cfd);
}

// 设置非阻塞
int setnonblocking(int fd){

	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
}

// 更改EPOLLONESHOT
void reset_OneShot(int efd, int cfd){
	// 是否去除oneshot
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
	ev.data.fd = cfd;
	int ret = epoll_ctl(efd, EPOLL_CTL_MOD, cfd, &ev);
	if (-1 == ret){
		perror("epoll_ctl add");
		return;
	}
}

// 根据fd打印客户端状态
void printCilentStatus(int fd, const char *status){
	char ip[16];
	memset(ip, 0, 16);
	struct sockaddr_in addr;
	socklen_t addrlen;
	int ret = getsockname(fd, (struct sockaddr *)&addr, &addrlen);
	if (-1 == ret){
		perror("getsockname");
	}
	printf("client [%s:%u] %s\n", inet_ntop(AF_INET, &(addr.sin_addr.s_addr), ip, 16),
		   ntohs(addr.sin_port), status);
}

// 发送http 头部包
void send_http_head(int cfd, int code, char *info, char *conenttype){

	char buf[256];
	int len_ = sprintf(buf, "HTTP/1.1 %d %s\r\n", code, info);
	send(cfd, buf, len_, 0);

	len_ = sprintf(buf, "Content-Type:%s\r\n", conenttype);
	send(cfd, buf, len_, 0);

	// len_ = sprintf(buf,"Content-Length:%d\r\n", length);
	// send(cfd, buf, len_, 0);
	send(cfd, "\r\n", 2, 0);
}

// 线程处理客户端请求的回调
void threadwork_cb(void *arg){
	struct cfd_and_efd *p = (struct cfd_and_efd *)arg;
	read_client_request(p->efd, p->cfd);
	free(p);
}

// 处理contenttype
char *get_mime_type(char *name){
	char *dot;
	dot = strrchr(name, '.');
	if (dot == (char *)0)
		return "text/plain; charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}

// 发送文件数据
void send_file(int cfd, char *path, int efd, int isclose){
	// 0 拷贝技术
	struct stat statbuf;
	int ret = stat(path, &statbuf);
	if (-1 == ret){
		perror("stat");
		return;
	}
	int fd = open(path, O_RDONLY);
	if (-1 == fd){
		perror("open file");
		return;
	}
	ret = sendfile(cfd, fd, NULL, statbuf.st_size);
	if (-1 == ret){
		perror("sendfile");
	}

	close(fd);
	
	if (isclose){
		delFd(efd, cfd);
	}
}

// 回复客户端的请求
void send_http_data(int cfd, char *path, int efd){
	// 发送数据
	struct stat st;
	int ret = stat(path, &st);

	if (ret < 0){
		// 文件或目录不存在
		//printf("文件或目录不存在\n");
		// 先发送http头部
		send_http_head(cfd, 404, "Not Found", get_mime_type("error.html"));
		// 再发送数据部分
		send_file(cfd, "error.html", efd, 1);
	}
	else{
		// 文件或目录存在

		if (strcmp("./", path) == 0 || strcmp("./index.html", path) == 0){
			// index.html
			stat("index.html", &st);
			// 先发送http头部
			send_http_head(cfd, 200, "OK", get_mime_type("./index.html"));
			// 再发送数据部分
			send_file(cfd, "index.html", efd, 1);
		}
		else if (S_ISDIR(st.st_mode)){
			// 目录
			//printf("is dir\n");

			// 读目录
			DIR *dir = NULL;
			struct dirent *ptr = NULL;
			dir = opendir(path);
			char tmp[1024];
			int siz;
			// 发送头部
			send_http_head(cfd, 200, "OK", get_mime_type("*.html"));
			// 发送前部分数据
			send_file(cfd, "./test/dir_header.html", efd, 0);
			while (ptr = readdir(dir)){
				if (ptr == NULL)
					break;
				if (ptr->d_type == DT_DIR){
					siz = sprintf(tmp, "<li><a href=%s/>%s</a></li>", ptr->d_name, ptr->d_name);
				}
				else{
					siz = sprintf(tmp, "<li><a href=%s>%s</a></li>", ptr->d_name, ptr->d_name);
				}
				send(cfd, tmp, siz, 0);
			}
			// 发送后半部分数据
			send_file(cfd, "./test/dir_tail.html", efd, 1);

			closedir(dir);
		}
		else if (S_ISREG(st.st_mode)){
			// 常规文件
			//printf("is file\n");
			// 先发送http头部
			send_http_head(cfd, 200, "OK", get_mime_type(path));
			// 再发送数据部分
			send_file(cfd, path, efd, 1);
		}
		else{
			printf("出现错误\n");
		}
	}
}

