#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <math.h>
#include "ui.h"

#define MAX_MSG_LENGTH 256

#define CJOIN 1
#define RQNEW 2
#define CEXIT 3

pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct message{
  char* msg;
  char* usr;
}message_t;

typedef struct client{
  char* c_name;
  int   sockfd;
  pthread_mutex_t m;
  FILE* input;
  FILE* output;
}client_t;

typedef struct client_node
{
  struct client *c;
  struct client_node* next;
}client_list_t;

typedef struct thread_arg {
  int socket_fd;
  FILE* input;
  char* client_name;
} thread_arg_t;

typedef struct candidate{
  char* name;
  char* ip_addr;
  int id;
  int port_num;
}candidate_t;

typedef struct candidate_list{
  candidate_t *candidate;
  struct candidate_list *next;
}candidate_list_t;

client_t parent;
client_list_t* c_list = NULL;
int client_count = 0;
bool is_root = false;
int directory_id = -1;

char* my_name = "Anonymous";
int my_port = 0;
char* my_ip_addr = "";

void* parent_thread_fn(void* args);
void* main_child_thread_fn(void* args);
void* child_thread_fn(void* args);
candidate_list_t* connect_to_directory(int port, char* ip_addr, int command);
void connect_to_parent(candidate_list_t* candidates, int client_sock);

int main(int argc, char** argv) {

  if(argv[3]!=NULL){
    my_name = argv[3];
  }

  // Initialize the chat client's user interface.
  ui_init();
  // Add a test message
  pthread_mutex_lock(&ui_lock);
  ui_add_message(NULL, "Type your message and hit <ENTER> to post.");
  pthread_mutex_unlock(&ui_lock);

  int server_sock = socket(AF_INET, SOCK_STREAM, 0);
  int client_sock = socket(AF_INET, SOCK_STREAM, 0);
  if(server_sock == -1 || client_sock == -1) {
    perror("socket");
    exit(2);
  }
  // Listen at this address. We'll bind to port 0 to accept any available port
  struct sockaddr_in host_addr = {
    .sin_addr.s_addr = INADDR_ANY,
    .sin_family = AF_INET,
    .sin_port = htons(0)
  };

  // Bind to the specified address
  if(bind(server_sock, (struct sockaddr*)&host_addr, sizeof(struct sockaddr_in))) {
    perror("bind");
    exit(2);
  }

  listen(server_sock, 2);

  socklen_t addr_size = sizeof(struct sockaddr_in);
  getsockname(server_sock, (struct sockaddr *) &host_addr, &addr_size);

  my_port = ntohs(host_addr.sin_port);

  char ipstr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &host_addr.sin_addr, ipstr, INET_ADDRSTRLEN);

  my_ip_addr = ipstr;

  candidate_list_t* candidates = connect_to_directory(atoi(argv[2]), argv[1], CJOIN);

  if(candidates==NULL){
    is_root = true;
  }

  if(!is_root){
    connect_to_parent(candidates, client_sock);
  }
  // run child thread
  thread_arg_t* child_args = malloc(sizeof(thread_arg_t));
  child_args->socket_fd = server_sock;
  child_args->client_name = my_name;
  child_args->input = NULL;
  pthread_t main_child_thread;
  if(pthread_create(&main_child_thread, NULL, main_child_thread_fn, child_args)) {
    perror("pthread_create failed");
    exit(EXIT_FAILURE);
  }

  int error = 0;
  socklen_t len = sizeof(error);

  while(true){

    // Read a message from the UI
    char* message = ui_read_input();
    // If it is not the root check if parent is still connected otherise get a parent
    if(!is_root){
      int retval = getsockopt(client_sock, SOL_SOCKET, SO_ERROR, &error, &len);
      int tolerance = 0;
      while(retval != 0 && tolerance < 5){
        retval = getsockopt(client_sock, SOL_SOCKET, SO_ERROR, &error, &len);
        tolerance++;
      }

      if(retval!=0){
        fprintf(stderr, "getsockopt failed.");
        exit(2);
      }

      if(error != 0){

        candidate_list_t* new_candidates = connect_to_directory(atoi(argv[2]), argv[1], RQNEW);
        if(new_candidates==NULL){
          is_root = true;
        }
        if(!is_root){
          connect_to_parent(new_candidates, client_sock);
        }
      }
    }

    // If the message is a quit command, shut down. Otherwise print the message
    if(strcmp(message, "\\quit") == 0) {
      connect_to_directory(atoi(argv[2]), argv[1], CEXIT);
      break;
    } else if(strlen(message) > 0) {
      // Add the message to the UI
      pthread_mutex_lock(&ui_lock);
      ui_add_message(my_name, message);
      pthread_mutex_unlock(&ui_lock);
      size_t message_length = strlen(message);
      size_t name_length    = strlen(my_name);
      char *named_message = (char*)malloc((name_length + message_length)*sizeof(char) + 2);
      strcpy(named_message, my_name);
      strcpy(named_message+name_length, "#!");
      strcpy(named_message+name_length+2, message);
      if(!is_root){
        pthread_mutex_lock(&(parent.m));
        fprintf(parent.output, "%s\n", named_message);
        fflush(parent.output);
        pthread_mutex_unlock(&(parent.m));
      }
      // propagate 'my_msg' to children
      client_list_t* temp = c_list;
      while(temp != NULL){
        pthread_mutex_lock(&(temp->c->m));
        fprintf(temp->c->output, "%s\n", named_message);
        fflush(temp->c->output);
        pthread_mutex_unlock(&(temp->c->m));
        temp = temp->next;
      }
      free(named_message);
    }
  }
  // Free the message
  //free(message);
  // Clean up the UI
  close(server_sock);
  close(client_sock);
  while(c_list != NULL){
    close(c_list->c->sockfd);
    c_list = c_list->next;
  }
  ui_shutdown();
}

void* parent_thread_fn(void* p){
  // Unpack the thread arguments
  thread_arg_t* args = (thread_arg_t*)p;
  int socket_fd = args->socket_fd;
  char* client_name = args->client_name;
  //free(args);
  // Read lines until we hit the end of the input (the client disconnects)
  char* line = NULL;
  size_t linecap = 0;
  while(getline(&line, &linecap, parent.input) > 0) {
    char *parse_line  = strdup(line);
    char *parent_name = strtok(parse_line, "#!");
    char *parent_msg  = strtok(NULL, "#!");
    pthread_mutex_lock(&ui_lock);
    ui_add_message(parent_name, parent_msg);
    pthread_mutex_unlock(&ui_lock);
    //propogate 'new_msg' to all children
    client_list_t* temp = c_list;
    while(temp != NULL){
      pthread_mutex_lock(&(temp->c->m));
      fprintf(temp->c->output, "%s", line);
      fflush(temp->c->output);
      pthread_mutex_unlock(&(temp->c->m));
      temp = temp->next;
    }
  }
  // When we're done, we should free the line from getline
  //free(line);

  return NULL;
}

void* main_child_thread_fn(void* p){
  while(true){
    thread_arg_t* child_args = (thread_arg_t*)p;
    int server_sock = child_args->socket_fd;
    char* client_name = child_args->client_name;
    // Accept a client connection
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    int client_socket = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);

    // create struct for client
    client_t *newclient = (client_t*)malloc(sizeof(client_t));
    newclient->sockfd = client_socket;
    newclient->c_name = "client";
    pthread_mutex_init(&newclient->m, NULL);
    // Duplicate the socket_fd so we can open it twice, once for input and once for output
    int client_socket_copy = dup(client_socket);
    if(client_socket_copy == -1) {
      perror("dup failed 1");
      exit(EXIT_FAILURE);
    }

    // Open the socket as a FILE stream so we can use fgets
    FILE* client_input = fdopen(client_socket, "r");
    FILE* client_output = fdopen(client_socket_copy, "w");

    // Check for errors
    if(client_input == NULL || client_output == NULL) {
      perror("fdopen failed");
      exit(EXIT_FAILURE);
    }
    newclient->input = client_input;
    newclient->output = client_output;
    // add client to list of clients
    client_list_t* newnode = (client_list_t*)malloc(sizeof(client_list_t));
    newnode->c = newclient;
    newnode->next = c_list;
    c_list = newnode;

    // Set up arguments for the client thread
    thread_arg_t* args = malloc(sizeof(thread_arg_t));
    args->socket_fd = client_socket;
    args->client_name = "a";
    args->input = client_input;

    // Create the child thread
    pthread_t child_thread;
    if(pthread_create(&child_thread, NULL, child_thread_fn, args)) {
      perror("pthread_create failed");
      exit(EXIT_FAILURE);
    }
    client_count++;
  }
  return NULL;
}

void* child_thread_fn(void* p){
  // Unpack the thread arguments
  thread_arg_t* args = (thread_arg_t*) p;
  int socket_fd = args->socket_fd;
  char* client_name = args->client_name;
  FILE* input = args->input;
  //free(args);
  // Read lines until we hit the end of the input (the client disconnects)
  char* line = NULL;
  size_t linecap = 0;
  while(getline(&line, &linecap, input) > 0) {
    //propogate 'new_msg' to all children
    char *parse_line = strdup(line);
    char *child_name = strtok(parse_line, "#!");
    char *child_msg  = strtok(NULL, "#!");
    pthread_mutex_lock(&ui_lock);
    ui_add_message(child_name, child_msg);
    pthread_mutex_unlock(&ui_lock);
    if(!is_root){
      pthread_mutex_lock(&(parent.m));
      fprintf(parent.output, "%s", line);
      fflush(parent.output);
      pthread_mutex_unlock(&(parent.m));
    }
    client_list_t* temp = c_list;
    while(temp != NULL){
      if(temp->c->sockfd != socket_fd){
        pthread_mutex_lock(&(temp->c->m));
        fprintf(temp->c->output, "%s", line);
        fflush(temp->c->output);
        pthread_mutex_unlock(&(temp->c->m));
      }
      temp = temp->next;
    }
  }
  // When we're done, we should free the line from getline
  //free(line);

  return NULL;
}

candidate_list_t* connect_to_directory(int port, char* ip_addr, int command){
  int client_sock = socket(AF_INET, SOCK_STREAM, 0);
  if(client_sock == -1){
    perror("socket failed.");
    exit(EXIT_FAILURE);
  }

  // Initialize socket address (with address to be specified from server)
  struct sockaddr_in client_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(port)
  };

  if(connect(client_sock, (struct sockaddr *)&client_addr, sizeof(struct sockaddr_in))){
    perror("connect failed");
    exit(2);
  }

  // struct hostent *server = gethostbyname("IP address returned from DIRSRV");
  struct hostent *server = gethostbyname(ip_addr);
  if (server == NULL) {
    fprintf(stderr, "Unable to find host %s\n", ip_addr);
    exit(EXIT_FAILURE);
  }
  bcopy((char *)server->h_addr, (char *)&client_addr.sin_addr.s_addr, server->h_length);

  // Duplicate the socket_fd so we can open it twice, once for input and once for output
  int client_sock_copy = dup(client_sock);
  if(client_sock_copy == -1) {
    perror("dup failed 3");
    exit(EXIT_FAILURE);
  }
  FILE* input = fdopen(client_sock, "r");
  FILE* output = fdopen(client_sock_copy, "w");
  // Check for errors
  if(input == NULL || output == NULL) {
    perror("fdopen failed");
    exit(EXIT_FAILURE);
  }

  char *line = NULL;
  size_t linecap = 0;

  fprintf(output, "%d\n", command);
  fflush(output);

  if(command == CJOIN){
    getline(&line, &linecap, input);
    directory_id = atoi(line);

    int id_len = 1;
    if(directory_id > 0){
      id_len = log10(directory_id);
    }

    int port_len = (int) log10(my_port);

    char *id = (char*)malloc(sizeof(char)*id_len + 2);
    char *port = (char*)malloc(sizeof(char)*port_len + 2);
    sprintf(id, "%d", directory_id);
    sprintf(port, "%d", my_port);

    int reqstring_len = strlen(my_name) + strlen(my_ip_addr) + strlen(id) + strlen(port) + 8;

    char *reqstring = (char*)malloc(reqstring_len*sizeof(char) + 1);

    strcpy(reqstring, my_name);
    strcpy(reqstring + strlen(my_name), "#!");
    strcpy(reqstring + strlen(my_name) + 2, my_ip_addr);
    strcpy(reqstring + strlen(my_name) + strlen(my_ip_addr) + 2, "#!");
    strcpy(reqstring + strlen(my_name) + strlen(my_ip_addr) + 4, id);
    strcpy(reqstring + strlen(my_name) + strlen(my_ip_addr) + strlen(id) + 4, "#!");
    strcpy(reqstring + strlen(my_name) + strlen(my_ip_addr) + strlen(id) + 6, port);
    strcpy(reqstring + strlen(my_name) + strlen(my_ip_addr) + strlen(id) + strlen(port) + 6, "#!");

    fprintf(output, "%s\n", reqstring);
    fflush(output);

  }else if(command == RQNEW){
    fprintf(output, "%d\n", directory_id);
    fflush(output);
  }else{
    fprintf(output, "%d\n", directory_id);
    fflush(output);
    return NULL;
  }

  candidate_list_t* root = NULL;

  while(getline(&line, &linecap, input) > 0){
    char *parse_line = line;
    candidate_t* new_candidate = (candidate_t*)malloc(sizeof(candidate_t));

    new_candidate->name = strtok(parse_line, "#!");
    new_candidate->ip_addr = strtok(NULL, "#!");
    new_candidate->id = atoi(strtok(NULL, "#!"));
    new_candidate->port_num = atoi(strtok(NULL, "#!"));
    if(new_candidate->id != directory_id){
      candidate_list_t* new_node = (candidate_list_t*)malloc(sizeof(candidate_list_t));;
      new_node->candidate = new_candidate;
      new_node->next = NULL;
      if(root == NULL){
        root = new_node;
      } else {
        root->next = new_node;
      }
    }
  }
  close(client_sock);
  return root;
}

void connect_to_parent(candidate_list_t* candidates, int client_sock){
  int rand_index = random() % directory_id;
  candidate_list_t *temp = candidates;
  for(int i = 0; i < rand_index; i++){
    candidates = candidates->next;
  }
  // Initialize socket address (with address to be specified from server)
  struct sockaddr_in client_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(candidates->candidate->port_num)
  };

  if(connect(client_sock, (struct sockaddr *)&client_addr, sizeof(struct sockaddr_in))){
    while(temp->next != NULL){
      if(temp->candidate->port_num == candidates->candidate->port_num){
        candidate_list_t* delete = temp->next;
        temp->candidate = temp->next->candidate;
        temp->next = delete->next;
        free(delete);
      }else{
        temp = temp->next;
      }
    }
    connect_to_parent(temp, client_sock);
  }

  // struct hostent *server = gethostbyname("IP address returned from DIRSRV");
  struct hostent *server = gethostbyname(candidates->candidate->ip_addr);
  if (server == NULL) {
    fprintf(stderr, "Unable to find host %s\n", candidates->candidate->ip_addr);
    exit(EXIT_FAILURE);
  }
  bcopy((char *)server->h_addr, (char *)&client_addr.sin_addr.s_addr, server->h_length);

  // create parent struct
  parent.c_name = candidates->candidate->name;
  parent.sockfd = client_sock;
  pthread_mutex_init(&parent.m, NULL);
  // Duplicate the socket_fd so we can open it twice, once for input and once for output
  int client_sock_copy = dup(client_sock);
  if(client_sock_copy == -1) {
    perror("dup failed 2");
    exit(EXIT_FAILURE);
  }
  FILE* parent_input = fdopen(client_sock, "r");
  FILE* parent_output = fdopen(client_sock_copy, "w");
  // Check for errors
  if(parent_input == NULL || parent_output == NULL) {
    perror("fdopen failed");
    exit(EXIT_FAILURE);
  }
  parent.input = parent_input;
  parent.output = parent_output;
  // run parent thread
  thread_arg_t* parent_args = malloc(sizeof(thread_arg_t));
  parent_args->input = parent_input;
  parent_args->client_name = my_name;
  parent_args->socket_fd = client_sock;
  pthread_t parent_thread;
  if(pthread_create(&parent_thread, NULL, parent_thread_fn, parent_args)) {
    perror("pthread_create failed");
    exit(EXIT_FAILURE);
  }
}
