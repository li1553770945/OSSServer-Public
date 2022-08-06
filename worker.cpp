#include "worker.h"
#include "easylogging++.h"
#include <sys/epoll.h> //epoll
#include <fcntl.h>	   //fcntl()
#include <thread>

Worker::Worker(int epoll_fd, Model *model, string machine_id)
{
	m_epoll_fd = epoll_fd;
	m_model = model;
	m_machine_id = machine_id;

	sem_init(&m_ready_fd_sem, 0, 0);
	for (int i = 1; i <= MAX_WORK_THREAD; i++) //启动处理线程
	{
		thread work_thread(&Worker::WorkThread, this);
		work_thread.detach();
	}
}
void Worker::WorkThread()
{
	Msg msg;
	int read_size;
	int ret;
	int fd;
	bool read_over;
	while (true)
	{
		sem_wait(&m_ready_fd_sem);
		if (m_ready_fd_q.try_dequeue(fd))
		{
			read_over = false;
			for (int i = 1; i <= MAX_PROC_ONCE && !read_over; i++)
			{
				read_size = 0;
				while (read_size < sizeof(Msg)) //读取一个msg
				{
					ret = recv(fd, (char *)&msg + read_size, sizeof(Msg) - read_size, 0);
					if (ret < 0)
					{
						if (errno == EAGAIN || errno == EWOULDBLOCK) //数据全部读取完毕
						{

							if (read_size == 0) //如果是一开始读
							{
								LOG(INFO) << "fd:" << fd << " read end,reset" << endl;
								ResetFd(fd);
								read_over = true;
								break;
							}
							else
							{
								continue; //如果msg读到一半缓冲区没数据了，继续读
							}
						}
						else //真的发生错误了
						{
							LOG(ERROR) << "socket recv error:" << errno << endl;
							read_over = true;
							RemoveFd(fd);
							break;
						}
					}
					else if (ret == 0) //对方断开连接
					{
						read_over = true;
						RemoveFd(fd);
						break;
					}
					else
					{
						read_size += ret;
					}
				}

				if (!read_over)
				{
					g_action_num++;
					switch (msg.type)
					{
					case Types::LoginRequest:
						Login(fd, msg);
						break;
					case Types::AddFileMetaRequest:
						if (CheckLogin(fd))
						{
							AddFileMeta(fd, msg);
						}
						else
						{
							read_over = true;
						}
						break;
					case Types::GetFileMetaRequest:
						if (CheckLogin(fd))
						{
							GetFileMeta(fd, msg);
						}
						else
						{
							read_over = true;
						}
						break;
					case Types::GetFileSroteServerRequest:
						if (CheckLogin(fd))
						{
							GetFileStoreServer(fd, msg);
						}
						else
						{
							read_over = true;
						}
						break;
					case Types::DeleteFileMetaRequest:
						if (CheckLogin(fd))
						{
							DeleteFileMeta(fd, msg);
						}
						else
						{
							read_over = true;
						}
						break;
					default:
						LOG(ERROR) << "unknow msg type:" << (int)msg.type << endl;
						break;
					}
				}
			}

			if (!read_over) //达到最大处理次数了，但是仍然没有处理完
			{
				m_ready_fd_q.enqueue(fd);
				sem_post(&m_ready_fd_sem);
			}
		}
		else
		{
			LOG(WARNING) << "deque failed,retry sem_post" << endl;
			sem_post(&m_ready_fd_sem);
		}
	}
}
bool Worker::CheckLogin(int fd)
{
	m_aks_mtx.lock_shared();
	if (m_aks[fd] == "")
	{
		m_aks_mtx.unlock_shared();
		LOG(WARNING) << "fd:" << fd << " not login but try to do something" << endl;
		RemoveFd(fd);
		return false;
	}
	else
	{
		m_aks_mtx.unlock_shared();
		return true;
	}
}
void Worker::Run()
{
	epoll_event events[MAX_EVENT_NUMBER];
	while (1)
	{
		int ret = epoll_wait(m_epoll_fd, events, MAX_EVENT_NUMBER, -1); // epoll_wait
		if (ret < 0)
		{
			LOG(ERROR) << "Server: epoll error" << endl;
			break;
		}
		for (int i = 0; i < ret; ++i)
		{
			int sockfd = events[i].data.fd;
			if (events[i].events & EPOLLIN)
			{

				m_ready_fd_q.enqueue(sockfd);
				sem_post(&m_ready_fd_sem);
			}
			else
			{
				LOG(WARNING) << "epoll event not epollin" << endl;
			} //是in事件
		}	  //一次wait多个事件的循环
	}		  //主while循环
}

string Worker::GetToken(string ak, string sk)
{
	return ak;
}
string Worker::GetAk(string token)
{
	return token;
}

void Worker::Login(int fd, Msg &msg) //登陆
{

	LOG(INFO) << "fd:" << fd << ":login" << endl;
	LoginReq *req = (LoginReq *)msg.data;
	string ak = req->ak, sk = req->sk;
	Msg response;
	response.type = Types::LoginResponse;
	response.id = msg.id;
	LoginRes *res = (LoginRes *)response.data;
	res->err = m_model->Login(ak, sk);
	if (res->err == Errors::Success)
	{
		m_aks_mtx.lock();
		m_aks[fd] = ak;
		m_aks_mtx.unlock();
	}
	if (res->err == Errors::AuthFail)
	{
		strcpy(res->msg, "ak or/and sk error");
	}
	if (send(fd, &response, sizeof(Msg), 0) < 0)
	{
		LOG(ERROR) << "send to client error:" << errno << endl;
	}
}
void Worker::AddFileMeta(int fd, Msg &msg)
{
	LOG(INFO) << "fd:" << fd << ":add file meta" << endl;
	AddFileMetaReq *req = (AddFileMetaReq *)(msg.data);
	m_aks_mtx.lock_shared();
	string file_id = m_model->AddFileMeta(m_aks[fd], req->file_name, req->file_size, m_machine_id);
	m_aks_mtx.unlock_shared();
	Msg response;
	response.type = Types::AddFileMetaResponse;
	response.len = file_id.length();
	response.id = msg.id;
	strcpy(response.data, file_id.data());
	if (send(fd, &response, sizeof(Msg), 0) < 0)
	{
		LOG(ERROR) << "send to client error:" << errno << endl;
	}
}
void Worker::DeleteFileMeta(int fd, Msg &msg)
{

	LOG(INFO) << "fd:" << fd << ":delete file meta" << endl;
	DeleteFileMetaReq *req = (DeleteFileMetaReq *)(msg.data);
	m_aks_mtx.lock_shared();
	m_model->DeleteFileMeta(m_aks[fd], req->file_id);
	m_aks_mtx.unlock_shared();
}
void Worker::GetFileMeta(int fd, Msg &msg)
{

	LOG(INFO) << "fd:" << fd << ":get file meta" << endl;
	GetFileMetaReq *req = (GetFileMetaReq *)(msg.data);
	Msg response;
	response.type = Types::GetFileMetaResponse;
	response.id = msg.id;
	GetFileMetaRes *res = (GetFileMetaRes *)response.data;
	m_aks_mtx.lock_shared();
	res->err = m_model->GetFileMeta(m_aks[fd], req->file_id, res);
	m_aks_mtx.unlock_shared();
	if (send(fd, &response, sizeof(Msg), 0) < 0)
	{
		LOG(ERROR) << "send to client error:" << errno << endl;
	}
}
void Worker::GetFileStoreServer(int fd, Msg &msg)
{
	LOG(INFO) << "fd:" << fd << ":get file store server" << endl;
	GetFileStoreServerReq *req = (GetFileStoreServerReq *)(msg.data);
	Msg response;
	response.type = Types::GetFileSroteServerResponse;
	response.id = msg.id;
	GetFileStoreServerRes *res = (GetFileStoreServerRes *)response.data;
	res->size = 0;
	vector<IpPort> ips;
	m_aks_mtx.lock_shared();
	m_model->GetFileStoredServer(m_aks[fd], req->file_id, ips);
	m_aks_mtx.unlock_shared();
	res->size = ips.size();
	for (int i = 0; i < ips.size() && i < 4; i++)
	{
		res->ip[i].ip = ips[i].ip;
		res->ip[i].port = ips[i].port;
	}

	if (send(fd, &response, sizeof(Msg), 0) < 0)
	{
		LOG(ERROR) << "send to client error:" << errno << endl;
	}
}

void Worker::RemoveFd(int fd)
{
	LOG(INFO) << "fd:" << fd << " disconnected" << endl;
	m_aks_mtx.lock();
	m_aks[fd] = "";
	m_aks_mtx.unlock();
	g_tcp_num--;
	close(fd);
}
void Worker::ResetFd(int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &event);
}