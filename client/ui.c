#include "ui.h"

#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIDTH 78
#define CHAT_HEIGHT 24
#define INPUT_HEIGHT 1
#define USERNAME_DISPLAY_MAX 8

WINDOW* mainwin;
WINDOW* chatwin;
WINDOW* inputwin;
  
char* messages[CHAT_HEIGHT];
size_t num_messages = 0;

/**
 * Initialize the chat user interface. Call this once at startup.
 */
void ui_init() {
  // Create the main window
  mainwin = initscr();
  if(mainwin == NULL) {
    fprintf(stderr, "Failed to initialize screen\n");
    exit(EXIT_FAILURE);
  }
  
  // Don't display characters when they're pressed
  noecho();
  
  // Create the chat window
  chatwin = subwin(mainwin, CHAT_HEIGHT + 2, WIDTH + 2, 0, 0);
  box(chatwin, 0, 0);
  
  // Create the input window
  inputwin = subwin(mainwin, INPUT_HEIGHT + 2, WIDTH + 2, CHAT_HEIGHT + 2, 0);
  box(inputwin, 0, 0);
  
  // Refresh the display
  refresh();
}

// Clear the chat window (refresh required)
void ui_clear_chat() {
  for(int i=0; i<WIDTH; i++) {
    for(int j=0; j<CHAT_HEIGHT; j++) {
      mvwaddch(chatwin, 1+j, 1+i, ' ');
    }
  }
}

/**
 * Add a message to the chat window. If username is NULL, the message is
 * indented by two spaces.
 *
 * \param username  The username string. Truncated to 8 characters by default.
 *                  This function does *not* take ownership of this memory.
 * \param message   The message string. This function does *not* take ownership
 *                  of this memory.
 */
void ui_add_message(char* username, char* message) {
  // Clear the chat window
  ui_clear_chat();
  
  // Free the oldest message if it will be lost
  if(num_messages == CHAT_HEIGHT) {
    free(messages[CHAT_HEIGHT-1]);
  } else {
    num_messages++;
  }
  
  // Move messages up
  memmove(&messages[1], &messages[0], sizeof(char*) * (CHAT_HEIGHT - 1));
  
  // Make space for the username and message
  messages[0] = malloc(sizeof(char*) * (WIDTH + 1));
  
  // Keep track of where we are in the message string
  size_t offset = 0;
  
  // Add the username, or indent two spaces if there isn't one
  if(username == NULL) {
    messages[0][0] = ' ';
    messages[0][1] = ' ';
    offset = 2;
  } else if(strlen(username) > USERNAME_DISPLAY_MAX) {
    strncpy(messages[0], username, USERNAME_DISPLAY_MAX-3);
    offset = USERNAME_DISPLAY_MAX-3;
    messages[0][offset++] = '.';
    messages[0][offset++] = '.';
    messages[0][offset++] = '.';
    messages[0][offset++] = ':';
    messages[0][offset++] = ' ';
  } else {
    strcpy(messages[0], username);
    offset = strlen(username);
    messages[0][offset++] = ':';
    messages[0][offset++] = ' ';
  }
  
  // If characters remain, add them in another message
  if(strlen(message) > WIDTH - offset) {
    strncpy(&messages[0][offset], message, WIDTH - offset);
    ui_add_message(NULL, &message[WIDTH - offset]);
  } else {
    strcpy(&messages[0][offset], message);
    
    // Display the messages
    for(int i=0; i<num_messages; i++) {
      mvwaddstr(chatwin, CHAT_HEIGHT - i, 1, messages[i]);
    }
    wrefresh(chatwin);
    wrefresh(inputwin);
  }
}

// Clear the input window (refresh required)
void ui_clear_input() {
  for(int i=0; i<WIDTH; i++) {
    mvwaddch(inputwin, 1, 1+i, ' ');
  }
}

/**
 * Read an input line, with some upper bound determined by the UI.
 *
 * \returns A pointer to allocated memory that holds the line. The caller is
 *          responsible for freeing this memory.
 */
char* ui_read_input() {
  int length = 0;
  int c;
  
  // Allocate space to hold an input line
  char* buffer = malloc(sizeof(char) * (WIDTH + 1));
  buffer[0] = '\0';
  
  // Loop until we get a newline
  while((c = getch()) != '\n') {
    // Is this a backspace or a new character?
    if(c == KEY_BACKSPACE || c == KEY_DC || c == 127) {
      // Delete the last character
      if(length > 0) {
        length--;
        buffer[length] = '\0';
      }
    } else if(length < WIDTH) {
      // Add the new character, unless we're at the length limit
      buffer[length] = c;
      buffer[length+1] = '\0';
      length++;
    }
    
    // Clear the previous input and re-display it
    ui_clear_input();
    mvwaddstr(inputwin, 1, 1, buffer);
    wrefresh(inputwin);
  }
  
  // Clear the input and refresh the display
  ui_clear_input();
  wrefresh(inputwin);
  
  return buffer;
}

/**
 * Shut down the user interface. Call this once during shutdown.
 */
void ui_shutdown() {
  // Clean up windows and ncurses stuff
  delwin(inputwin);
  delwin(chatwin);
  delwin(mainwin);
  endwin();
  refresh();
}
