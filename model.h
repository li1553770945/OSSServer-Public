#pragma once

#include <iostream>
#include <list>
#include <semaphore.h>
#include <queue>
#include "utils.h"
#include <mysql/mysql.h>
#include "database.h"
#include "concurrentqueue.h"

class Model
{
private:
	static atomic_int file_num;
	static moodycamel::ConcurrentQueue<string> m_sync_sqls;
	static MySqlPool *m_pool;
	static sem_t m_sqls_sem;

private:
	static void Execer(); //执行m_sync_sqls的进程
	void AddSqls(string sqls);

public:
	Model(string host, string user, string pwd, string dbname, int port);

	Errors Login(string ak, string sk);
	string GetToken(string access_key, string secret_key);
	string GetAk(string token);
	string AddFileMeta(string access_key, string filename, int file_size, string machine_id);
	Errors DeleteFileMeta(string access_key, string file_id);
	Errors GetFileMeta(string access_key, string file_id, GetFileMetaRes *res);
	Errors GetFileStoredServer(string access_key, string file_id, vector<IpPort> &result);
	~Model();
};