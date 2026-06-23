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

/*
 * Define os tipos de mensagens internas do servidor.
 * ENTER representa a entrada de um cliente novo.
 * LEAVE representa a saida/desconexao de um cliente.
 * 
 * Esse enum é utilizado pela função broadcastServerMsg,
 * para envio de mensagens do servidor para os clientes.
 */
typedef enum e_msgType{
	ENTER,
	LEAVE
}	t_msgType;

/*
 * Guarda as informacoes de cada cliente conectado.
 * fd é o descritor do socket do cliente.
 * id é o numero usado nas mensagens publicas, como "client 0".
 * buffer acumula dados recebidos até formar uma mensagem completa com '\n'.
 */
typedef struct s_client {
	int fd;
	int id;
	char *buffer;
}	t_client;

/*
 * Socket principal do servidor.
 * É nele que o programa escuta novas conexoes de clientes.
 */
int sockfd;

/*
 * Conjuntos de file descriptors usados pelo select.
 * all_fds guarda todos os sockets monitorados.
 * selected_fds recebe, a cada volta do loop, apenas os sockets que estão
 * prontos para leitura.
 */
fd_set all_fds, selected_fds;

/*
 * Maior file descriptor atualmente monitorado.
 * O select precisa desse valor + 1 para saber ate onde deve procurar eventos.
 */
int max_fd = 0;

/*
 * Array com todos os clientes possiveis do servidor.
 * O tamanho FD_SETSIZE acompanha o limite usado pelos conjuntos fd_set.
 * 
 * FD_SETSIZE é um valor definido pelo sistema operacional e representa
 * o numero maximo de file descriptors que podem ser monitorados por select.
 * Ele é definido no arquivo <sys/select.h> e normalmente vale 1024.
 */
t_client all_clients[FD_SETSIZE];

/*
 * Tamanho maximo usado para receber dados de um cliente em uma chamada recv.
 */
int max_buffer = 20000;

/*
 * Procura uma mensagem completa dentro de buf, considerando '\n' como fim.
 * Quando encontra uma linha completa, coloca essa linha em msg e deixa em buf
 * apenas o restante que ainda nao foi processado.
 * Retorna 1 quando extrai uma mensagem, 0 quando ainda nao ha linha completa
 * e -1 em caso de erro de alocacao.
 * 
 * A presente função já foi dada pela main e segue inalterada
 */
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

/*
 * Junta a string add ao final de buf, criando uma nova area de memoria.
 * A funcao libera o buf antigo depois de copiar seu conteudo.
 * Retorna o novo buffer com as strings concatenadas ou NULL em caso de erro.
 * 
 * A presente função já foi dada pela main e segue inalterada
 */
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

/*
 * Escreve uma mensagem de erro na saida de erro padrao.
 * E usada para centralizar chamadas simples ao write com strlen.
 */
void writeErr(char *txt)
{
	write(2, txt, strlen(txt));
}

/*
 * Mostra a mensagem "Fatal error" e encerra o programa com codigo de erro.
 * Deve ser chamada quando o servidor nao consegue continuar com seguranca.
 */
void fatalError()
{
	writeErr("Fatal error\n");
	exit (1);
}

/*
 * Envia msg para todos os clientes conectados, exceto para o fd informado.
 * Isso evita reenviar a mensagem para o próprio cliente que originou o evento.
 */
void sendFinalMsg(char *msg, int fd)
{
	for (int i = 0; i < FD_SETSIZE; i++)
	{
		if (all_clients[i].fd == -1 || all_clients[i].fd == fd)
			continue ;
		send(all_clients[i].fd, msg, strlen(msg), 0);
	}
}

/*
 * Monta e envia uma mensagem do servidor avisando que um cliente entrou ou saiu.
 * msgType define o tipo do aviso, fd é o socket ignorado no broadcast e id é o
 * identificador publico do cliente.
 */
void broadcastServerMsg(t_msgType msgType, int fd, int id)
{
	char tmp[100];
	if (msgType == ENTER)
		sprintf(tmp, "server: client %d just arrived\n", id);
	else if (msgType == LEAVE)
		sprintf(tmp, "server: client %d just left\n", id);
	sendFinalMsg(tmp, fd);
}

/*
 * Recebe dados de um cliente, acumula no buffer dele e envia apenas mensagens
 * completas para os outros clientes. Cada linha enviada recebe o prefixo
 * "client X: ", onde X é o id do cliente.
 */
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
}

/*
 * Registra um novo cliente na primeira posicão livre do array all_clients.
 * Tambem atribui um id sequencial, adiciona o fd ao conjunto do select e avisa
 * os outros clientes que alguém entrou.
 */
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

/*
 * Verifica se o socket principal recebeu uma nova conexão.
 * Quando há conexao pendente, aceita o cliente e chama addNewClient para
 * registrá-lo no servidor.
 */
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

/*
 * Remove o cliente da posição i do array.
 * A função avisa os demais clientes, tira o fd do select, fecha o socket e
 * libera o buffer pendente desse cliente.
 */
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

/*
 * Percorre todos os clientes registrados e verifica quem enviou dados.
 * Se recv indicar desconexao ou erro, significa que o cliente se
 * desconectou. Nesse caso, ele é removido. Do contrário,
 * repassa os dados recebidos para broadcastClientMsg.
 */
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

/*
 * Loop principal do servidor.
 * Usa select para esperar atividade no socket principal ou nos clientes, depois
 * trata novas conexões e novas mensagens.
 */
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

/*
 * Ponto de entrada do programa.
 * Valida os argumentos, cria o socket TCP em 127.0.0.1:porta, faz bind/listen,
 * inicializa as estruturas globais e inicia o loop principal do servidor.
 */
int main(int argc, char *argv[]) {
	struct sockaddr_in servaddr;

	/*
	 * O programa precisa receber exatamente um argumento: a porta.
	 * Se a porta não for informada, escreve a mensagem exigida pelo
	 * enunciado na saída de erro e encerra com código 1.
	 */
	if (argc != 2)
	{
		writeErr("Wrong number of arguments\n");
		exit (1);
	}

	/*
	 * Cria o socket principal do servidor.
	 * AF_INET indica IPv4, SOCK_STREAM indica TCP e 0 usa o protocolo padrão.
	 * Se a criação falhar, o servidor não consegue continuar.
	 */
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
		fatalError();

	/*
	 * Zera a estrutura de endereço antes de preencher seus campos.
	 */
	bzero(&servaddr, sizeof(servaddr)); 

	/*
	 * Configura o endereço do servidor.
	 * sin_family define IPv4, sin_addr.s_addr define 127.0.0.1 e
	 * sin_port define a porta recebida por argumento.
	 */
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
  
	/*
	 * Associa o socket recém-criado ao endereço e à porta configurados.
	 * Depois do bind, o sistema operacional sabe em qual porta o servidor
	 * deve receber conexões.
	 */
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		fatalError();

	/*
	 * Coloca o socket em modo de escuta.
	 * A partir daqui, clientes podem tentar se conectar ao servidor.
	 */
	if (listen(sockfd, 10) != 0)
		fatalError();

	/*
	 * Inicializa todos os espaços de clientes como vazios.
	 * fd e id recebem -1 para indicar que a posição ainda não possui cliente.
	 */
	for (int i = 0; i < FD_SETSIZE; i++)
	{
		all_clients[i].fd = -1;
		all_clients[i].id = -1;
		all_clients[i].buffer = NULL;
	}

	/*
	 * Limpa os conjuntos de file descriptors antes de começar a usá-los.
	 * all_fds guardará todos os sockets monitorados pelo servidor.
	 * selected_fds será usado como cópia temporária em cada chamada de select.
	 */
	FD_ZERO(&all_fds);
	FD_ZERO(&selected_fds);

	/*
	 * Adiciona o socket principal ao conjunto monitorado.
	 * Assim, o select conseguirá avisar quando houver uma nova conexão.
	 */
	FD_SET(sockfd, &all_fds);

	/*
	 * Como neste momento só o socket principal está sendo monitorado,
	 * ele também é o maior file descriptor conhecido.
	 */
	max_fd = sockfd;

	/*
	 * Inicia o loop principal do servidor.
	 * A função mainLoop só termina se o programa for encerrado.
	 */
	mainLoop();
}
