#ifndef _SMTP_
#include "smtp.h"
#endif

#ifndef _IOSTREAM_
#include <iostream>
#endif

#ifndef _CTIME_
#include <ctime>
#endif

#ifndef _FSTREAM_
#include <fstream>
#endif

#pragma comment (lib,"Ws2_32.lib")

#pragma comment (lib,"libcrypto.lib")
#pragma comment (lib,"libssl.lib")

void GetTimeStamp(char *output, const char * format)
{
	time_t now_time;
	struct tm info;

	//获取当前时间戳 换算为当地地址 转化成tm 结构
	time(&now_time);
	localtime_s(&info, &now_time);

	//按照指定格式输出到 传入缓冲中
	strftime(output, LOG_T_MAXLEN - 1, format, &info);
}


void LoadSocket(int major_version, int minor_version)
{
	WSADATA wsadata;
	WORD socket_version = MAKEWORD(major_version, minor_version);
	int error;

	error = WSAStartup(socket_version, &wsadata);
	if (error)
	{
		std::cout << "ERROR wsastartup failed with error: " << error << std::endl;
		exit(1);
	}
}

void LoadSSL()
{
	/*SSL 库初始化*/
	SSL_library_init();
	/*载入所有SSL 算法*/
	OpenSSL_add_all_algorithms();
	/*载入所有SSL错误消息*/
	SSL_load_error_strings();
}


SmtpServer& operator<<(SmtpServer& server, const char *data_send)
{
	//发送数据
	SSL_write(server.ssl_, data_send, strlen(data_send));

	//记录日志，输出到标准输出
	GetTimeStamp(server.log_time_buffer_, LOG_T_F);
	server.log_file_ << server.log_time_buffer_ << "INFO send:  " << data_send;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
	std::cout << "INFO";
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
	std::cout << "	send:  " << data_send;

	return server;
}


int operator>>(SmtpServer& server, char *data_receive)
{
	//接收数据，如果没有数据则阻塞挂起
	int data_len = 0;
	data_len = SSL_read(server.ssl_, data_receive, server.buffer_size_);
	GetTimeStamp(server.log_time_buffer_, LOG_T_F);

	//记录日志，输出到标准输出
	data_receive[data_len] = '\0';
	server.log_file_ << server.log_time_buffer_ << "INFO receive:  " << data_receive;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
	std::cout << "INFO";
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
	std::cout << " receive:  " << data_receive;

	
	return data_len;
}


SmtpServer::SmtpServer(int buffer_size,const char *crt,const char *key) :listen_socket_(INVALID_SOCKET), buffer_size_(buffer_size), buffer_(NULL)
{
	//通过样式 LOG_FN_F 获取LOG文件名
	char log_fn[30];
	GetTimeStamp(log_fn, LOG_FN_F);

	//打开Log文件
	log_file_.open(log_fn);
	if (!log_file_.is_open())
	{
		exit(FILE_OPEN_ERROR);
	}

	//获取socket 地址族ipv4 流式SOCKET 协议TCP
	listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket_ == INVALID_SOCKET)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR socket failed with error : "
			<< WSAGetLastError() << std::endl;

		WSACleanup();
		exit(SOCKET_CREAT_ERROR);
	}


	//以 SSL V2 和 V3 标准兼容方式产生 SSL_CTX
	server_ctx_ = SSL_CTX_new(SSLv23_server_method());
	if (server_ctx_ == NULL)
	{
		ERR_print_errors_fp(stdout);
		exit(1);
	}
	//载入用户的数字证书
	if (SSL_CTX_use_certificate_file(server_ctx_, crt, SSL_FILETYPE_PEM) <= 0)
	{
		ERR_print_errors_fp(stdout);
		exit(SSL_ERROR);
	}
	//载入用户私钥
	if (SSL_CTX_use_PrivateKey_file(server_ctx_, key, SSL_FILETYPE_PEM) <= 0) 
	{
		ERR_print_errors_fp(stdout);
		exit(SSL_ERROR);
	}
	//检查用户私钥是否正确
	if (!SSL_CTX_check_private_key(server_ctx_)) 
	{
		ERR_print_errors_fp(stdout);
		exit(SSL_ERROR);
	}


	//申请缓冲内存
	buffer_ = new char[buffer_size_];
	if (buffer_ == NULL)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR failed to new a " << buffer_size_
			<< "bytes buffer" << std::endl;

		WSACleanup();
		exit(BUFFER_GET_ERROR);
	}

}


void SmtpServer::Listen(unsigned short listen_port)
{
	//设置地址和端口
	listen_addr_ = "127.0.0.1";
	listen_port_ = listen_port;

	//服务器端口地址初始化
	sockaddr_in svr_adr;
	memset(&svr_adr, 0, sizeof(svr_adr));
	
	//地址族 ipv4 
	svr_adr.sin_family = AF_INET;
	svr_adr.sin_port = htons(listen_port_);
	svr_adr.sin_addr.S_un.S_addr = inet_addr(listen_addr_);


	//绑定端口和地址
	if (bind(listen_socket_, (LPSOCKADDR)&svr_adr, sizeof(svr_adr)) == SOCKET_ERROR)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR bind failed with error: "
			<< WSAGetLastError() << std::endl;

		closesocket(listen_socket_);
		WSACleanup();
		exit(SOCKET_BIND_ERROR);
	}

	//开始监听端口
	if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR listen failed with error: "
			<< WSAGetLastError() << std::endl;

		closesocket(listen_socket_);
		WSACleanup();
		exit(SOCKET_LISTEN_ERROR);
	}

	GetTimeStamp(log_time_buffer_, LOG_T_F);
	log_file_<< log_time_buffer_<< "INFO server listenning on " << listen_addr_
		<< ":" << listen_port_ << "......" << std::endl;

	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
	std::cout << "INFO";
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
	std::cout << " listenning on " << listen_addr_
		<< ":" << listen_port_ << "......" << std::endl;
	

}


void SmtpServer::Start(CallBack server_logic, CallBack client_logic, SmtpServer& svr)
{
	//客户端地址初始化
	sockaddr_in host_addr;
	int host_addr_len = sizeof(host_addr);
	memset(&host_addr, 0, host_addr_len);

	while (1)
	{
		session_socket_ = INVALID_SOCKET;

		//接收连接，如果没有连接则阻塞挂起
		session_socket_ = accept(listen_socket_, (SOCKADDR*)&host_addr, &host_addr_len);
		GetTimeStamp(log_time_buffer_, LOG_T_F);

		//连接失败则跳过，继续处理下一个连接
		if (session_socket_ == INVALID_SOCKET)
		{
			log_file_ << GetTimeStamp << "WARNING accept failed with error: "
				<< WSAGetLastError() << std::endl;

			continue;
		}

		log_file_ << log_time_buffer_ << "INFO accepted a connection from " << inet_ntoa(host_addr.sin_addr)
			<< ":" << ntohs(host_addr.sin_port) << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
		std::cout << "INFO";
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
		std::cout << " a connection from " << inet_ntoa(host_addr.sin_addr)
			<< ":" << ntohs(host_addr.sin_port) << std::endl;

		//建立SSL连接
		if (BuildSsl() != 0)
		{
			closesocket(session_socket_);
			continue;
		}

		//然后调用回调函数开始SMTP SERVER逻辑
		if (server_logic(svr) == 0)
		{
			//关闭客户端SOCKET
			closesocket(session_socket_);
			log_file_ << log_time_buffer_ << "INFO mail receive succeed" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
			std::cout << "\nINFO mail receive success\n" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);

			//只有从客户端接收邮件成功，才进一步发生与远程服务器的通信
			//连接远程服务器  调用回调函数 开始SMTP Client逻辑
			if (ConnectRemote() == 0)
			{
				//连接成功，开始发送邮件
				if (client_logic(svr) == 0)
				{
					//关闭远程服务器SOCKET
					closesocket(session_socket_);
					log_file_ << log_time_buffer_ << "INFO mail send succeed" << std::endl;
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
					std::cout << "\nINFO mail send succeed\n" << std::endl;
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
				}
				else
				{
					//发送邮件失败也关闭远程服务器SOCKET
					closesocket(session_socket_);
					log_file_ << log_time_buffer_ << "WARNING mail send failed" << std::endl;
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED);
					std::cout << "INFO mail send failed" << std::endl;
					SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
				}
			}
		}
		else
		{
			//接收邮件失败也关闭客户端SOKCET
			closesocket(session_socket_);

			GetTimeStamp(log_time_buffer_, LOG_T_F);
			log_file_ << log_time_buffer_ << "WARNING mail receive failed" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED);
			std::cout << "WARNING mail receive failed" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
		}
	}
}


int SmtpServer::BuildSsl()
{
	//基于服务器CTX 生成一个SSL会话
	ssl_ = SSL_new(server_ctx_);
	//将连接用户的socket 加入到SSL
	SSL_set_fd(ssl_, session_socket_);
	//建立 SSL 连接
	if (SSL_accept(ssl_) == -1)
	{
		std::cout << "ERROR SSL Connect failed!" << std::endl;
		return 1;
	}
	return 0;
}


int SmtpServer::SaveMailData(char *mail_list)
{
	int data_len = 0;
	int data_count = 0;
	data_file_.open(mail_list,std::ios::app);
	if (!data_file_.is_open())
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR open data file failed" << std::endl;
	}
	data_file_ << END_OF_DATA;

	//标记邮件起点
	data_file_.width(8);
	data_file_ << 0 << std::endl;

	//接收一个或多个邮件数据包
	while (true)
	{
		data_len = SSL_read(ssl_, buffer_, buffer_size_);
		//如果意外断开连接
		if (data_len == -1)
		{
			GetTimeStamp(log_time_buffer_, LOG_T_F);
			log_file_ << log_time_buffer_ << "WARRING disconnected from the client" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED);
			std::cout<< "WARRING disconnected from the client" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);

			return 1;
		}

		data_count += data_len;
		buffer_[data_len] = '\0';

		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "INFO receiving data......... " << data_len <<" bytes"<<std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
		std::cout << "INFO";
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | 5);
		std::cout << " receiving data......... " << data_len << " bytes" << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
		
		//写入文件
		data_file_ << buffer_;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
		std::cout << "INFO";
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
		std::cout << " receive:  ";
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | 1);
		std::cout << buffer_;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);

		//检查数据结束标志
		if (strcmp(CHECK_DATA_END(buffer_, data_len), END_OF_DATA) == 0)
		{
			//标记邮件结束点
			data_file_.width(8);
			data_file_ << data_count << std::endl;
			data_file_.close();
			

			GetTimeStamp(log_time_buffer_, LOG_T_F);
			log_file_ << log_time_buffer_ << "INFO finished  ..... total: " << data_count << " bytes" << std::endl;

			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
			std::cout << "INFO";
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | 5);
			std::cout << " finished  ..... total: " << data_count << " bytes" << std::endl;
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
			break;
		}
	}

	return 0;
}


int SmtpServer::ReadMailData(char *mail_list)
{
	int mail_size;
	int offset;
	data_file_.open(mail_list, std::ios::in);
	if (!data_file_.is_open())
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "WARRING disconnected from the client" << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED);
		std::cout << "WARRING disconnected from the client" << std::endl;
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);

		return 1;
	}

	//获取邮件大小
	data_file_.seekg(-10, std::ios::end);
	data_file_ >> mail_size;

	//定位到邮件起点
	/*用邮件大小就可以直接定位到邮件起点
	 *但是这里seekg函数向上定位跨度确小于传入参数
	 *猜测是Windows文件格式的原因
	 *所以这里先使用邮件大小初步定位，再用一个for循环精细比较邮件起点
	 *(为了蒙混过关就用了这个蠢办法 别打我QAQ 嘤嘤嘤~
	 */
	char cmp[END_OF_DATA_L+1];
	cmp[END_OF_DATA_L] = '\0';
	for (offset = -mail_size;; offset--)
	{
		data_file_.seekg(offset, std::ios::end);
		data_file_.read(cmp, END_OF_DATA_L);
		if (strcmp(cmp, END_OF_DATA) == 0)
		{
			offset += 17;
			break;
		}
	}

	//读取邮件内容
	data_file_.seekg(offset, std::ios::end);
	data_file_.read(buffer_, mail_size);
	buffer_[mail_size] = '\0';

	data_file_.close();
	return 0;
}


int SmtpServer::ConnectRemote()
{
	session_socket_ = INVALID_SOCKET;
	remote_addr_ = "220.181.12.17";
	remote_port_ = 25;

	session_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (session_socket_ == INVALID_SOCKET)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR session socket failed with error : "
			<< WSAGetLastError() << std::endl;

		WSACleanup();
		exit(SOCKET_CREAT_ERROR);
	}

	//初始化远程地址
	sockaddr_in remote_adr;
	memset(&remote_adr, 0, sizeof(remote_adr));

	remote_adr.sin_family = AF_INET;
	remote_adr.sin_port = htons(remote_port_);
	remote_adr.sin_addr.S_un.S_addr = inet_addr(remote_addr_);

	//连接远程SMTP服务器
	if (connect(session_socket_, (LPSOCKADDR)&remote_adr, sizeof(remote_adr)) == SOCKET_ERROR)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "ERROR connect failed with error: "
			<< WSAGetLastError() << std::endl;

		closesocket(listen_socket_);
		closesocket(session_socket_);
		WSACleanup();
		exit(REMOTE_CONNECT_ERROR);
	}
	if (session_socket_ == INVALID_SOCKET)
	{
		GetTimeStamp(log_time_buffer_, LOG_T_F);
		log_file_ << log_time_buffer_ << "WARNING unable to connect to the remote: " << std::endl;

		return 1;
	}

	//连接成功
	GetTimeStamp(log_time_buffer_, LOG_T_F);
	log_file_ << log_time_buffer_ << "INFO server connected to " << remote_addr_
		<< ":" << remote_port_ << "......" << std::endl;

	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_GREEN);
	std::cout << "INFO server connected to " << remote_addr_
		<< ":" << remote_port_ << "......" << std::endl;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);

	return 0;

}


SmtpServer::~SmtpServer()
{
	delete[]buffer_;
	log_file_.close();
	closesocket(listen_socket_);
	WSACleanup();
}