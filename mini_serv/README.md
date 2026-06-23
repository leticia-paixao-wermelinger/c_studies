# mini_serv

Este exercício pede a implementação de um pequeno servidor TCP local, escutando em `127.0.0.1` na porta passada por argumento. O servidor aceita vários clientes ao mesmo tempo e permite que eles conversem entre si.

Quando um cliente entra, os outros recebem:

```txt
server: client X just arrived
```

Quando um cliente sai, os outros recebem:

```txt
server: client X just left
```

Quando um cliente envia uma mensagem, o servidor repassa cada linha para os demais clientes com o prefixo:

```txt
client X: mensagem
```

## Como compilar

O arquivo esperado pelo exercício é `mini_serv.c`. Para compilar manualmente:

```sh
cc -Wall -Wextra -Werror -g mini_serv.c
```


## Como executar

O programa recebe a porta como primeiro argumento:

```sh
./mini_serv 12345
```

Em outros terminais, conecte clientes usando `nc`:

```sh
nc 127.0.0.1 12345
```

Abra dois ou mais terminais com `nc` para testar a troca de mensagens.

OBS: Algumas portas não estão liberadas na rede da 42, como a porta 4242.

## Conceitos básicos

### Servidor TCP

TCP é um protocolo orientado à conexão. Isso significa que, antes de trocar mensagens, o cliente precisa se conectar ao servidor.

Neste exercício, o servidor:

1. cria um socket com `socket`;
2. associa esse socket ao endereço `127.0.0.1` e a uma porta com `bind`;
3. coloca o socket em modo de escuta com `listen`;
4. aceita clientes novos com `accept`;
5. recebe mensagens com `recv`;
6. envia mensagens para outros clientes com `send`;
7. fecha conexões com `close`.

### `127.0.0.1`

O endereço `127.0.0.1` é o endereço de loopback, também chamado de localhost. Ele aponta para a própria máquina. Neste exercício, o servidor não deve escutar em uma rede externa, apenas localmente.

### File descriptor

Um file descriptor, ou `fd`, é um número inteiro usado pelo sistema operacional para representar recursos abertos. No caso deste exercício, cada socket tem um `fd`.

O servidor tem:

- um `fd` para o socket principal, que recebe novas conexões;
- um `fd` para cada cliente conectado.

### `select`

O `select` permite monitorar vários file descriptors ao mesmo tempo. Em vez de ficar preso esperando apenas um cliente, o servidor pergunta ao sistema:

> Quais sockets estão prontos para leitura agora?

Assim, o servidor consegue tratar:

- clientes novos tentando conectar;
- clientes antigos enviando mensagens;
- clientes desconectando.

### `fd_set`

`fd_set` é o tipo usado junto com `select` para guardar conjuntos de file descriptors.

As macros principais são:

- `FD_ZERO`: limpa um conjunto;
- `FD_SET`: adiciona um `fd` ao conjunto;
- `FD_CLR`: remove um `fd` do conjunto;
- `FD_ISSET`: verifica se um `fd` está presente no conjunto depois do `select`.

### `accept`

`accept` é usado quando o socket principal detecta uma nova conexão.

O socket criado com `socket`, associado com `bind` e colocado em escuta com `listen` não é o socket usado para conversar diretamente com o cliente. Ele serve como uma porta de entrada para novas conexões.

Quando `accept` é chamado, ele cria um novo file descriptor para o cliente conectado. Esse novo `fd` é o que será usado depois em `recv`, `send`, `close` e no `select`.

No exercício, a ideia é:

1. `select` avisa que o socket principal está pronto;
2. isso significa que há uma conexão nova esperando;
3. `accept` aceita essa conexão;
4. o novo cliente é registrado no array `all_clients`.

### `recv`

`recv` recebe dados de um socket conectado. Neste exercício, ela é usada para ler mensagens enviadas pelos clientes.

Ela retorna a quantidade de bytes recebidos. Se o retorno for menor ou igual a zero, o código entende que o cliente desconectou ou que houve erro, então remove esse cliente.

Um ponto importante: `recv` não garante que a mensagem venha inteira. Uma chamada pode receber meia linha, uma linha completa ou várias linhas juntas. Por isso o programa guarda os dados no `buffer` de cada cliente e só envia aos outros quando encontra `\n`.

### `send`

`send` envia dados por um socket conectado. Neste exercício, ela é usada para mandar mensagens do servidor para os clientes.

O código usa `send` para:

- avisar quando um cliente entrou;
- avisar quando um cliente saiu;
- repassar mensagens enviadas por um cliente para todos os outros.

Como o enunciado diz para não desconectar clientes lentos, o programa usa `select` antes de trabalhar com os sockets e evita chamadas feitas sem controle.

### `sprintf`

`sprintf` monta uma string formatada dentro de um buffer de caracteres.

Ela funciona de forma parecida com `printf`, mas em vez de imprimir na tela, escreve o resultado dentro de uma string.

No exercício, `sprintf` é usado para montar mensagens como:

```c
sprintf(tmp, "server: client %d just arrived\n", id);
sprintf(prevMsg, "client %d: ", id);
```

O `%d` é substituído pelo número do cliente. Assim, o servidor consegue montar mensagens diferentes para cada `id`.

### Buffer de mensagens

Nem sempre uma chamada de `recv` recebe uma mensagem completa. Também é possível receber várias mensagens de uma vez.

Por isso, cada cliente tem um `buffer`. O programa acumula os dados recebidos até encontrar um `\n`, que marca o fim de uma linha. Depois disso, a linha pode ser enviada aos outros clientes.

## Estruturas do programa

### `t_msgType`

Enum usado para diferenciar mensagens internas do servidor:

- `ENTER`: cliente entrou;
- `LEAVE`: cliente saiu.

Ele é usado pela função `broadcastServerMsg`.

### `t_client`

Struct que representa um cliente conectado:

```c
typedef struct s_client {
	int fd;
	int id;
	char *buffer;
}	t_client;
```

Campos:

- `fd`: socket do cliente;
- `id`: número público do cliente, usado nas mensagens;
- `buffer`: texto acumulado até formar uma linha completa.

### Variáveis globais

- `sockfd`: socket principal do servidor;
- `all_fds`: conjunto com todos os sockets monitorados;
- `selected_fds`: cópia usada pelo `select` para indicar sockets prontos;
- `max_fd`: maior file descriptor monitorado;
- `all_clients`: array com os clientes conectados;
- `max_buffer`: tamanho máximo usado em uma chamada de `recv`.

## Funções do código

### `extract_message`

Procura uma linha completa dentro de um buffer. Uma linha completa termina com `\n`.

Retornos:

- `1`: encontrou uma mensagem completa;
- `0`: ainda não há mensagem completa;
- `-1`: erro de alocação.

### `str_join`

Concatena o conteúdo antigo do buffer com os novos dados recebidos pelo `recv`.

Ela aloca uma nova string, copia os dados antigos, adiciona os novos dados e libera o buffer antigo.

### `writeErr`

Escreve uma mensagem na saída de erro padrão, usando `write`.

### `fatalError`

Mostra `Fatal error` e encerra o programa com `exit(1)`.

### `sendFinalMsg`

Envia uma mensagem para todos os clientes conectados, exceto para o cliente indicado pelo `fd`.

### `broadcastServerMsg`

Monta mensagens automáticas do servidor, como entrada e saída de clientes, e envia para os demais clientes.

### `broadcastClientMsg`

Recebe os dados de um cliente, junta no buffer dele, separa por linhas completas e envia cada linha para os outros clientes com o prefixo `client X:`.

### `addNewClient`

Adiciona um cliente novo ao array `all_clients`, atribui um `id`, adiciona o `fd` ao conjunto do `select` e avisa os outros clientes.

### `checkNewClient`

Verifica se o socket principal está pronto para aceitar uma nova conexão. Se estiver, chama `accept` e registra o cliente.

### `removeClient`

Remove um cliente desconectado, fecha o socket, limpa o `fd` do conjunto monitorado e libera o buffer pendente.

### `checkNewMsg`

Percorre os clientes conectados e verifica se algum deles enviou dados. Se recebeu dados, repassa para `broadcastClientMsg`. Se o cliente desconectou, chama `removeClient`.

### `mainLoop`

É o loop principal do servidor. Ele chama `select`, verifica novas conexões e verifica novas mensagens.

### `main`

Valida os argumentos, cria o socket principal, configura endereço e porta, faz `bind`, faz `listen`, inicializa as estruturas globais e inicia o loop principal.


## Manuais úteis para este exercício

```sh
man select
man recv
man send
man sprintf
```


## Pontos de atenção

- A main original vem com diversos erros que precisam ser corrigidos.
- O tester não aceita nenhuma mensagem de debug printada no terminal do servidor, que deve ficar limpo durante a execução (com a exceção dos casos de erro indicados no subject).
- Cada cliente precisa receber um `id` sequencial. Mesmo que um cliente tenha encerrado a sua conexão, outro cliente futuro não reaproveita o id do cliente encerrado.
- É terminantemente proibido o uso de signal.
- O tester avalia casos mais pesados do que os testes manuais simples: mensagens simultâneas, mensagens muuuuuuuuuuuuito grandes, rapidez de resposta e situações em que várias mensagens chegam quase ao mesmo tempo. Por isso, pode acontecer timeout ou o trace ficar difícil de ler, com muita coisa impressa de forma intercalada por causa da interpolação das mensagens.
- Um detalhe importante da estratégia: antes, eu juntava todas as mensagens em uma única string grande e só enviava no final, mas assim eu não conseguia passar no tester. A solução funcionou quando mudei para enviar cada pequeno pedaço da mensagem com `send`, conforme as linhas iam sendo processadas.
