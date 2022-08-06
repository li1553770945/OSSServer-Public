#include "easylogging++.h"
#include "server.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

INITIALIZE_EASYLOGGINGPP

int main(int argc, char *argv[])
{
    srand(time(0));
    el::Configurations conf("./log.conf");
    el::Loggers::reconfigureAllLoggers(conf);

    Server server("9.135.35.137", 13001, "127.0.0.1", 12001, "9.135.135.245", 3306, "root", "Li5060520", "oss");
    LOG(INFO) << "server start";
    server.Run();
}