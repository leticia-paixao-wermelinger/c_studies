#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdlib.h>

typedef enum e_msgType{
	ENTER,
	LEAVE
}	t_msgType;

typedef struct s_client {
	int fd;
	int id;
	char *buffer;
}	t_client;

int sockfd;
fd_set all_fds, selected_fds;
int max_fd = 0;
t_client all_clients[FD_SETSIZE];

int max_buffer = 20000;

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void writeErr(char *txt)
{
	write(2, txt, strlen(txt));
}

void fatalError()
{
	writeErr("Fatal error\n");
	exit (1);
}

void sendFinalMsg(char *msg, int fd)
{
	for (int i = 0; i < FD_SETSIZE; i++)
	{
		if (all_clients[i].fd == -1 || all_clients[i].fd == fd)
			continue ;
		send(all_clients[i].fd, msg, strlen(msg), 0);
	}
}

void broadcastServerMsg(t_msgType msgType, int fd, int id)
{
	char tmp[100];
	if (msgType == ENTER)
		sprintf(tmp, "server: client %d just arrived\n", id);
	else if (msgType == LEAVE)
		sprintf(tmp, "server: client %d just left\n", id);
	sendFinalMsg(tmp, fd);
}

void broadcastClientMsg(char *msg, int fd, int id, int i)
{
	all_clients[i].buffer = str_join(all_clients[i].buffer, msg);

	char *line = NULL;

	char prevMsg[30];
	sprintf(prevMsg, "client %d: ", id);

	while (extract_message(&all_clients[i].buffer, &line) == 1)
	{
		sendFinalMsg(prevMsg, fd);
		sendFinalMsg(line, fd);
		if (line)
		{
			free(line);
			line = NULL;
		}
	}
	// if (ret < 0)
	// 	fatalError();
}

void addNewClient(int fd)
{
	static int next_id = 0;
	for (int i = 0; i < FD_SETSIZE; i++)
	{
		if (all_clients[i].fd == -1)
		{
			broadcastServerMsg(ENTER, fd, next_id);
			all_clients[i].fd = fd;
			all_clients[i].id = next_id;
			next_id++;
			all_clients[i].buffer = NULL;
			FD_SET(fd, &all_fds);
			if (max_fd <= fd)
				max_fd = fd;
			return ;
		}
	}
}

void checkNewClient()
{
	if (FD_ISSET(sockfd, &selected_fds))
	{
		int connfd;
		unsigned int len;
		struct sockaddr_in cli;

		len = sizeof(cli);
		connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
		if (connfd < 0)
			fatalError();
		addNewClient(connfd);
	}
}

void removeClient(int i)
{
	broadcastServerMsg(LEAVE, all_clients[i].fd, all_clients[i].id);
	FD_CLR(all_clients[i].fd, &all_fds);
	close(all_clients[i].fd);
	all_clients[i].fd = -1;
	all_clients[i].id = -1;
	if (all_clients[i].buffer)
	{
		free(all_clients[i].buffer);
		all_clients[i].buffer = NULL;
	}
}

void checkNewMsg()
{
	for (int i = 0; i < FD_SETSIZE; i++)
	{
		if (all_clients[i].fd == -1)
			continue;
		if (FD_ISSET(all_clients[i].fd, &selected_fds))
		{
			char buffer[max_buffer - 1];
			int buffSize = recv(all_clients[i].fd, buffer, max_buffer, 0);
			if (buffSize <= 0)
			{
				removeClient(i);
				return ;
			}
			buffer[buffSize] = '\0';
			broadcastClientMsg(buffer, all_clients[i].fd, all_clients[i].id, i);
		}
	}
}

void mainLoop()
{
	while (42)
	{
		selected_fds = all_fds;
		int ret_selected = select(max_fd + 1, &selected_fds, NULL, NULL, NULL);
		if (!ret_selected)
			fatalError();
		checkNewClient();
		checkNewMsg();
	}
}

int main(int argc, char *argv[]) {
	struct sockaddr_in servaddr;

	if (argc != 2)
	{
		writeErr("Wrong number of arguments\n");
		exit (1);
	}

	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
		fatalError();
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		fatalError();

	if (listen(sockfd, 10) != 0)
		fatalError();

	for (int i = 0; i < FD_SETSIZE; i++)
	{
		all_clients[i].fd = -1;
		all_clients[i].id = -1;
		all_clients[i].buffer = NULL;
	}

	FD_ZERO(&all_fds);
	FD_ZERO(&selected_fds);

	FD_SET(sockfd, &all_fds);

	max_fd = sockfd;

	mainLoop();

	// len = sizeof(cli);
	// connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
	// if (connfd < 0) { 
    //     printf("server acccept failed...\n"); 
    //     exit(0); 
    // } 
    // else
    //     printf("server acccept the client...\n");
}