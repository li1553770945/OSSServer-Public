#include <iostream>
#include "model.h"
#include "concurrentqueue.h"
#include <unordered_map>
#include <shared_mutex>

using namespace std;

extern atomic_uint64_t g_tcp_num, g_action_num;

const int MESSAGE_LEN = sizeof(Msg);
const int MAX_EVENT_NUMBER = 10240;
const int MAX_WORK_THREAD = 32;
const int MAX_PROC_ONCE = 128; //每次最多处理事件数

class Worker
{
private:
	int m_epoll_fd;
	string m_machine_id;
	Model *m_model;
	moodycamel::ConcurrentQueue<int> m_ready_fd_q;
	sem_t m_ready_fd_sem;
	unordered_map<int, string> m_aks;
	shared_mutex m_aks_mtx;

private:
	string GetAk(string);
	string GetToken(string ak, string sk);

	void WorkThread();

	bool CheckLogin(int fd);
	void Login(int fd, Msg &msg);
	void AddFileMeta(int fd, Msg &msg);
	void DeleteFileMeta(int fd, Msg &msg);
	void GetFileMeta(int fd, Msg &msg);
	void GetFileStoreServer(int fd, Msg &msg);

	void RemoveFd(int fd);
	void ResetFd(int fd);

public:
	Worker(int epoll_fd, Model *model, string machine_id);
	void Run();
};