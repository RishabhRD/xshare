#include <iostream>
#include <thread>
#include <dirent.h>
#include <unordered_map>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unordered_set>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctime>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
using namespace std;
const string NOT_FOUND = "HTTP/1.1 404 Not Found\nConnection: close\nServer: XShare Server\nContent-length: 48\n\n<html><body><h1>404 NOT FOUND</h1></body></html>";
const string NOT_IMPLEMENTED = "HTTP/1.1 501 Not Implemented\nConnection: close\nServer: XShare Server\nContent-length: 55\n\n<html><body><h1>METHOD NOT SUPPORTED</h1></body></html>";
const string COMPRESSION_NOT_ALLOWED = "HTTP/1.1 200 OK\nConnection: close\nServer: XShare Server\nContent-length: 58\n\n<html><body><h1>Compression Not Allowed</h1></body></html>";
const string COMPRESSING  = "HTTP/1.1 200 OK\nConnection: Keep-Alive\nServer: XShare Server\nContent-length: 46\nKeep-Alive: timeout=40, max=60\n\n<html><body><h1>Compressing</h1></body></html>";
string INAME,PATH,PASSWORD;
int PORT = 12312;
bool SINGLE_LAYER = 0;
string my_ip;
unordered_set<string> stored_machines;
bool PERMIT_COMPRESSED = false;
void eraseSubStr(std::string & mainStr, const std::string & toErase)
{
	// Search for the substring in string
	size_t pos = mainStr.find(toErase);

	if (pos != std::string::npos)
	{
		// If found then erase it from string
		mainStr.erase(pos, toErase.length());
	}
}
inline bool ends_with(std::string const & value, std::string const & ending)
{
	if (ending.size() > value.size()) return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}
bool parse_arguments(int argc,char** argv){
	unordered_map<string,int> map;
	for(int i=1;i<argc;i++){
		map[string(argv[i])] = i;
	}
	if(map.find("-h")!=map.end()){
		cout<<"XShare is a file-sharing application created by Rishabh Dwivedi.\nOptions:"<<endl;
		cout<<"-a                    setting password"<<endl;
		cout<<"-d                    provide port (default port is 12312)"<<endl;
		cout<<"-h                    help"<<endl;
		cout<<"-i                    provide interface name"<<endl;
		cout<<"-p                    path of sharing directory"<<endl;
		cout<<"-s                    single layer share"<<endl;
		cout<<"--permit-compressed   Automatically permits to compress directory and send";
		return true;
	}
	auto itr = map.find("-i");
	if(map.find("-i")==map.end()){
		cout<<"Provide interface name"<<endl;
		return true;
	}else{
		INAME =  string(argv[(itr->second)+1]);
	}
	itr = map.find("-p");
	if(itr != map.end()){
		PATH = string(argv[(itr->second)+1]);
	}else{
		PATH = ".";
	}
	itr = map.find("-a");
	if(itr!=map.end()){
		PASSWORD = string(argv[(itr->second)+1]);
	}else{
		PASSWORD = " ";
	}
	itr = map.find("-s");
	if(itr !=map.end()){
		SINGLE_LAYER = 1;
	}
	itr = map.find("-d");
	if(itr!=map.end()){
		PORT = atoi(argv[(itr->second) +1]);
	}
	itr = map.find("--permit-compressed");
	if(itr!=map.end()){
		PERMIT_COMPRESSED = true;
	}
	return false;
}
void do_compression(string curpath,int sock){
	/*struct sockaddr_in addr;
	memset(&addr,0,sizeof addr);
	int addrlen = sizeof addr;
	getpeername(sock,(struct sockaddr*)&addr,(socklen_t*)&addrlen);*/
	string zipname = curpath;
	zipname.erase(zipname.length()-1);
	zipname = zipname+".zip";
	zipname = basename(zipname.c_str());
	string cmd = "zip -r -j "+zipname+" "+curpath;
	system(cmd.c_str());

	string contentMimeType = "application/zip";
	int fd = open(zipname.c_str(),O_RDONLY);
	struct stat s;
	fstat(fd,&s);
	char* adr = (char*)mmap(NULL,s.st_size,PROT_READ,MAP_SHARED,fd,0);
	string filesize = to_string(s.st_size);
	auto start = std::chrono::system_clock::now();
	time_t date = chrono::system_clock::to_time_t(start);
	string dateStr = std::ctime(&date);
	string header = "HTTP/1.1 200 OK\nConnection: close\nServer: XShare Server\nDate: "+dateStr+"Content-Disposition: attachement; filename=\""+zipname+"\"\n"+ "Content-type: "+contentMimeType+"\nContent-length: "+filesize+"\n\n";
	send(sock,header.c_str(),header.length(),0);
	int numSend = 32765;
	int i =0;
	while(i < s.st_size){
		if(s.st_size-i>=numSend){
			send(sock,adr+i,numSend,0);
			i+=numSend;
		}else{
			send(sock,adr+i,s.st_size-i,0);
			i+=s.st_size-i;
		}	
	}
	close(fd);
	close(sock);
	cmd = "rm "+zipname;
	system(cmd.c_str());
}

void handle_raw_data(char* data,int sock){
	string str = string(data);
	vector<string> vec;
	boost::split(vec,str,boost::is_any_of(" "));
	if(vec.at(0)!="GET"&&vec.at(0)!="HEAD"){
		send(sock,NOT_IMPLEMENTED.c_str(),NOT_IMPLEMENTED.length(),0);
	}
	string curpath;
	string requested_path = vec.at(1);
	bool compress = false;
	if(ends_with(requested_path,".get_compressed")){
		requested_path.erase(requested_path.end()-15,requested_path.end());
		compress = true;
	}
	boost::replace_all(requested_path,"%20"," ");
	curpath = (PATH+requested_path);
	cout<<"CURPATH: "<<curpath<<endl;
	struct stat st;
	int err = stat(curpath.c_str(),&st);
	if(err==-1){
		send(sock,NOT_FOUND.c_str(),NOT_FOUND.length(),0);	
	}else{
		if(S_ISDIR(st.st_mode)){
			if(compress){
				if(!PERMIT_COMPRESSED){
					cout<<"Do you want to compres "+curpath+" : ";
					string ans;
					cin>>ans;
					if(ans=="y"||ans=="Y"){
						do_compression(curpath,sock);
					}else{
						send(sock,COMPRESSION_NOT_ALLOWED.c_str(),COMPRESSION_NOT_ALLOWED.length(),0);
					}
				}else{
					do_compression(curpath,sock);
				}
			}else{
				if(SINGLE_LAYER){
					string body = "<html><head><title>Index Page</title></head><body><h1><font color = red>Index Page</font></h1><hr><br><table border = 1><tr><th>File Name</th></tr>";
					struct dirent* de;
					DIR* dr = opendir(curpath.c_str());
					while((de=readdir(dr))!=NULL){
						string curFile = curpath+string(de->d_name);
						struct stat tmpS;
						stat(curFile.c_str(),&tmpS);
						if(S_ISDIR(tmpS.st_mode)) continue;
						else{
							eraseSubStr(curFile,PATH);
							string link = "http://"+my_ip+":"+to_string(PORT)+curFile;
							body+="<tr><td><a href=\""+link+"\">"+de->d_name+"</a></td></tr>\n";
						}
					}
					body+="</table></body></html>";
					string httpLen = to_string(body.length());
					auto start = std::chrono::system_clock::now();
					time_t date = chrono::system_clock::to_time_t(start);
					string dateStr = std::ctime(&date);

					string header = "HTTP/1.1 200 OK\nConnection: close\nServer: XShare Server\nDate: "+dateStr+"Content-type: "+"text/html"+"\nContent-length: "+httpLen+"\n\n";
					string sendStr = header+body;
					send(sock,sendStr.c_str(),sendStr.length(),0);
				}else{
					cout<<"CURPATH: "<<curpath<<endl;
					string body = "<html><head><title>Index Page</title></head><body><h1><font color = red>Index Page</font></h1><hr><br><table border = 1><tr><th>File Name</th><th>File Type</th><th>Special Operation</th></tr>";
					struct dirent* de;
					DIR* dr = opendir(curpath.c_str());
					while((de=readdir(dr))!=NULL){
						string curFile = curpath+string(de->d_name);
						struct stat tmpS;
						stat(curFile.c_str(),&tmpS);
						if(S_ISDIR(tmpS.st_mode)){
							eraseSubStr(curFile,PATH);
							string link = "http://"+my_ip+":"+to_string(PORT)+curFile+"/";
							body+="<tr><td><a href=\""+link+"\">"+de->d_name+"</a></td><td>Directory</td><td><a href=\""+link+".get_compressed"+"\">Get Compressed</a></tr>\n";

						} 
						else{
							eraseSubStr(curFile,PATH);
							string link = "http://"+my_ip+":"+to_string(PORT)+curFile;
							body+="<tr><td><a href=\""+link+"\">"+de->d_name+"</a></td><td>File</td><td></td></tr>\n";
						}
					}
					body+="</table></body></html>";
					string httpLen = to_string(body.length());
					auto start = std::chrono::system_clock::now();
					time_t date = chrono::system_clock::to_time_t(start);
					string dateStr = std::ctime(&date);

					string header = "HTTP/1.1 200 OK\nConnection: close\nServer: XShare Server\nDate: "+dateStr+"Content-type: "+"text/html"+"\nContent-length: "+httpLen+"\n\n";
					string sendStr = header+body;
					send(sock,sendStr.c_str(),sendStr.length(),0);

				}
			}
		}else{
			string contentMimeType = (ends_with(requested_path,".html")||ends_with(requested_path,".htm"))?"text/html":"text/plain";
			int fd = open(curpath.c_str(),O_RDONLY);
			struct stat s;
			fstat(fd,&s);
			char* adr = (char*)mmap(NULL,s.st_size,PROT_READ,MAP_SHARED,fd,0);
			string filesize = to_string(s.st_size);
			auto start = std::chrono::system_clock::now();
			time_t date = chrono::system_clock::to_time_t(start);
			string dateStr = std::ctime(&date);
			string header = "HTTP/1.1 200 OK\nConnection: close\nServer: XShare Server\nDate: "+dateStr+"Content-type: "+contentMimeType+"\nContent-length: "+filesize+"Content-Disposition: attachement\n\n";
			send(sock,header.c_str(),header.length(),0);
			int numSend = 32765;
			int i =0;
			while(i < s.st_size){
				if(s.st_size-i>=numSend){
					send(sock,adr+i,numSend,0);
					i+=numSend;
				}else{
					send(sock,adr+i,s.st_size-i,0);
					i+=s.st_size-i;
				}	
			}
			close(fd);
		}
	}
	close(sock);
}
void handle_new_socket(int sock){
	cout<<"Got a new Connection"<<endl;
	const int read_len =  1024;
	char data[read_len]{0};
	try{

		int rd = read(sock,data,read_len);
		printf("%s\n",data);
		if(rd==0){
			close(sock);
			return;
		}
		if(PASSWORD == " "){
			handle_raw_data(data,sock);
		}else{

		}
	}catch(const char* e){
		printf("Exception: %s\n",e);
	}
}
int main(int argc,char** argv){
	if(parse_arguments(argc,argv)) return -1;
	if(ends_with(PATH,"/")){
		PATH.erase(PATH.length()-1);
	}
	int fd;
	struct ifreq ifr;
	fd = socket(AF_INET,SOCK_DGRAM,0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name,INAME.c_str(),IFNAMSIZ-1);
	if(ioctl(fd,SIOCGIFADDR,&ifr)<0){
		perror("Not a valid interface out interface with ipv4.");
		exit(EXIT_FAILURE);
	}
	close(fd);
	my_ip = string(inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr));
	int server_fd, new_socket ;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	if((server_fd = socket(AF_INET,SOCK_STREAM,0))==0){
		cout<<"Socket failed"<<endl;
		return -1;
	}
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(my_ip.c_str());
	address.sin_port = htons( PORT ); 
	memset(address.sin_zero,'\0',sizeof(address.sin_zero));
	if (bind(server_fd, (struct sockaddr *)&address,
				sizeof(address))<0)
	{
		perror("Port already in use");
		exit(EXIT_FAILURE);
	} 
	if (listen(server_fd, 40)!= 0) 
	{ 
		perror("New Client Trying to Connect. Queue Full, Connection Refused"); 
		exit(EXIT_FAILURE); 
	}
	cout<<"Listening"<<endl;
	while(1){
		cout<<"Reached here"<<endl;
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
						(socklen_t*)&addrlen))<0)
		{
			perror("Connection Interrupted");
			exit(EXIT_FAILURE);
		}
		thread t = thread(handle_new_socket,new_socket);
		t.detach();
	}	

}
