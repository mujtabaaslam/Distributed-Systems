#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>


#define CJOIN 1
#define RQNEW 2
#define CEXIT 3

typedef struct client{
  char* name;
  int id;
  int port;
  char* ip_addr;
}client_t;

typedef struct node{
  client_t* client;
  struct node *next;
}node_t;

int client_count = 0;
node_t* client_list = NULL;

int main(int argc, char* argv[]) {
  // Set up a socket
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s == -1) {
    perror("socket");
    exit(2);
  }

  // Listen at this address. We'll bind to port 0 to accept any available port
  struct sockaddr_in addr = {
    .sin_addr.s_addr = INADDR_ANY,
    .sin_family = AF_INET,
    .sin_port = htons(atoi(argv[1]))
  };

  // Bind to the specified address
  if(bind(s, (struct sockaddr*)&addr, sizeof(struct sockaddr_in))) {
    perror("bind");
    exit(2);
  }

  // Become a server socket
  listen(s, 2);

  // Get the listening socket info so we can find out which port we're using
  socklen_t addr_size = sizeof(struct sockaddr_in);
  getsockname(s, (struct sockaddr *) &addr, &addr_size);

  // Print the port information
  printf("Listening on port %d\n", ntohs(addr.sin_port));

  // Repeatedly accept connections
  while(true) {

    // Accept a client connection
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    int client_socket = accept(s, (struct sockaddr*)&client_addr, &client_addr_len);

    // Duplicate the client_socket so we can open it twice, once for input and once for output
    int client_socket_copy = dup(client_socket);
    if(client_socket_copy == -1){
      perror("dup failed");
      exit(EXIT_FAILURE);
    }

    // Open the socket as a FILE stream so we can use fgets
    FILE *input = fdopen(client_socket, "r");
    FILE *output = fdopen(client_socket_copy, "w");

    // Check for errors
    if(input == NULL || output == NULL){
      perror("fdopen failed");
      exit(EXIT_FAILURE);
    }

    // Read lines until we hit the end of the input (the client disconnects)
    char *line = NULL;
    size_t linecap = 0;
    getline(&line, &linecap, input);
    // Print a message on the server
    int command = atoi(line);
    int client_id = -1;
    if(command==CEXIT){
      getline(&line, &linecap, input);
      client_id = atoi(line);
      node_t *temp = client_list;
      while(temp->next != NULL){
        if(temp->client->id == client_id){
          node_t* delete = temp->next;
          temp->client = temp->next->client;
          temp->next = delete->next;
          free(delete);
        }else{
          temp = temp->next;
        }
      }
      close(client_socket);
    }
    else if(command==CJOIN){
      fprintf(output, "%d\n", client_count);
      fflush(output);

      char *parse_req = NULL;

      if(getline(&parse_req, &linecap, input) == -1){
        perror("getline failed.");
        exit(EXIT_FAILURE);
      }
      client_t *new_client = (client_t*)malloc(sizeof(client_t));

      new_client->name = strtok(parse_req, "#!");
      new_client->ip_addr = strtok(NULL, "#!");
      new_client->id = atoi(strtok(NULL, "#!"));
      new_client->port = atoi(strtok(NULL, "#!"));

      node_t *new_node = (node_t*)malloc(sizeof(node_t));
      new_node->client = new_client;
      new_node->next = NULL;
      if(client_list==NULL){
        client_list = new_node;
        client_id = client_count;
        client_count++;
      }else{
        node_t *temp = client_list;
        while(temp->next != NULL){
          temp = temp->next;
        }
        temp->next = new_node;
        client_id = client_count;
        client_count++;
      }

    }else if(command==RQNEW){
      getline(&line, &linecap, input);
      client_id = atoi(line);
    }
    node_t *temp = client_list;
    while(temp->next != NULL && temp->client->id < client_id && temp != NULL){
      char* name = temp->client->name;
      char* ip_addr = temp->client->ip_addr;
      int id_int = temp->client->id;
      int port_int = temp->client->port;

      int id_len, port_len = 1;
      if(id_int > 0){
        id_len = (int) log10(id_int);
      }
      if(port_int > 0){
        port_len = (int) log10(port_int);
      }

      char* id = (char*)malloc(sizeof(char)*id_len);
      char* port = (char*)malloc(sizeof(char)*port_len);

      sprintf(id, "%d", id_int);
      sprintf(port, "%d", port_int);

      int reqstring_len = strlen(name) + strlen(ip_addr) + strlen(id) + strlen(port) + 8;
      char *reqstring = (char*)malloc(reqstring_len*sizeof(char) + 1);
      strcpy(reqstring, name);
      strcpy(reqstring + strlen(name), "#!");
      strcpy(reqstring + strlen(name) + 2, ip_addr);
      strcpy(reqstring + strlen(name) + strlen(ip_addr) + 2, "#!");
      strcpy(reqstring + strlen(name) + strlen(ip_addr) + 4, id);
      strcpy(reqstring + strlen(name) + strlen(ip_addr) + strlen(id) + 4, "#!");
      strcpy(reqstring + strlen(name) + strlen(ip_addr) + strlen(id) + 6, port);
      strcpy(reqstring + strlen(name) + strlen(ip_addr) + strlen(id) + strlen(port) + 6, "#!");

      fprintf(output, "%s\n", reqstring);
      fflush(output);
      temp = temp->next;
    }
    free(line);
    fclose(input);
    fclose(output);
  }
  close(s);
}
