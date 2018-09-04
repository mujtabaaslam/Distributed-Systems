#ifndef UI_H
#define UI_H

/**
 * Initialize the chat user interface. Call this once at startup.
 */
void ui_init();

/**
 * Add a message to the chat window. If username is NULL, the message is
 * indented by two spaces.
 *
 * \param username  The username string. Truncated to 8 characters by default.
 *                  This function does *not* take ownership of this memory.
 * \param message   The message string. This function does *not* take ownership
 *                  of this memory.
 */
void ui_add_message(char* username, char* message);

/**
 * Read an input line, with some upper bound determined by the UI.
 *
 * \returns A pointer to allocated memory that holds the line. The caller is
 *          responsible for freeing this memory.
 */
char* ui_read_input();

/**
 * Shut down the user interface. Call this once during shutdown.
 */
void ui_shutdown();

#endif
