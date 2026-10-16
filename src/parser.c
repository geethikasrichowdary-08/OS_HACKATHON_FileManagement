#include "shell.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_APPEND,
    TOKEN_BACKGROUND
} TokenType;

typedef struct {
    TokenType type;
    char *text;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

static void set_error(char **error_message, const char *message)
{
    if (error_message != NULL) {
        free(*error_message);
        *error_message = strdup(message);
    }
}

static int append_char(char **buffer, size_t *length, size_t *capacity,
                       char value)
{
    if (*length + 1 >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        char *new_buffer = realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return -1;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }
    (*buffer)[(*length)++] = value;
    return 0;
}

static int add_token(TokenList *tokens, TokenType type, char *text)
{
    if (tokens->count == tokens->capacity) {
        size_t new_capacity = tokens->capacity == 0 ? 16 : tokens->capacity * 2;
        Token *new_items = realloc(tokens->items,
                                   new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return -1;
        }
        tokens->items = new_items;
        tokens->capacity = new_capacity;
    }
    tokens->items[tokens->count++] = (Token){.type = type, .text = text};
    return 0;
}

static void free_tokens(TokenList *tokens)
{
    for (size_t index = 0; index < tokens->count; index++) {
        free(tokens->items[index].text);
    }
    free(tokens->items);
}

static int tokenize(const char *line, TokenList *tokens, char **error_message)
{
    size_t index = 0;
    while (line[index] != '\0') {
        if (isspace((unsigned char)line[index])) {
            index++;
            continue;
        }

        TokenType operator_type;
        size_t operator_length = 1;
        bool is_operator = true;
        switch (line[index]) {
        case '|': operator_type = TOKEN_PIPE; break;
        case '<': operator_type = TOKEN_INPUT; break;
        case '>':
            if (line[index + 1] == '>') {
                operator_type = TOKEN_APPEND;
                operator_length = 2;
            } else {
                operator_type = TOKEN_OUTPUT;
            }
            break;
        case '&': operator_type = TOKEN_BACKGROUND; break;
        default: is_operator = false; operator_type = TOKEN_WORD; break;
        }
        if (is_operator) {
            if (add_token(tokens, operator_type, NULL) != 0) {
                set_error(error_message, "out of memory");
                return -1;
            }
            index += operator_length;
            continue;
        }

        char *word = NULL;
        size_t word_length = 0;
        size_t word_capacity = 0;
        bool started = false;
        char quote = '\0';
        while (line[index] != '\0') {
            char current = line[index];
            if (quote == '\0' && (isspace((unsigned char)current) ||
                                   current == '|' || current == '<' ||
                                   current == '>' || current == '&')) {
                break;
            }
            if (quote == '\0' && (current == '\'' || current == '"')) {
                quote = current;
                started = true;
                index++;
                continue;
            }
            if (quote != '\0' && current == quote) {
                quote = '\0';
                index++;
                continue;
            }
            if (current == '\\' && quote != '\'') {
                if (line[index + 1] == '\0') {
                    free(word);
                    set_error(error_message, "trailing escape character");
                    return -1;
                }
                index++;
                current = line[index];
            }
            if (append_char(&word, &word_length, &word_capacity, current) != 0) {
                free(word);
                set_error(error_message, "out of memory");
                return -1;
            }
            started = true;
            index++;
        }
        if (quote != '\0') {
            free(word);
            set_error(error_message, "unclosed quote");
            return -1;
        }
        if (!started) {
            free(word);
            set_error(error_message, "invalid token");
            return -1;
        }
        if (append_char(&word, &word_length, &word_capacity, '\0') != 0 ||
            add_token(tokens, TOKEN_WORD, word) != 0) {
            free(word);
            set_error(error_message, "out of memory");
            return -1;
        }
    }
    return 0;
}

static int add_argument(Command *command, char *argument)
{
    char **new_argv = realloc(command->argv,
                              (command->argc + 2) * sizeof(*new_argv));
    if (new_argv == NULL) {
        return -1;
    }
    command->argv = new_argv;
    command->argv[command->argc++] = argument;
    command->argv[command->argc] = NULL;
    return 0;
}

static int add_command(Pipeline *pipeline)
{
    Command *new_commands = realloc(
        pipeline->commands, (pipeline->count + 1) * sizeof(*new_commands));
    if (new_commands == NULL) {
        return -1;
    }
    pipeline->commands = new_commands;
    pipeline->commands[pipeline->count++] = (Command){0};
    return 0;
}

static int parse_tokens(TokenList *tokens, Pipeline *pipeline, const char *line,
                        char **error_message)
{
    if (tokens->count == 0) {
        return 0;
    }
    if (add_command(pipeline) != 0) {
        set_error(error_message, "out of memory");
        return -1;
    }

    for (size_t index = 0; index < tokens->count; index++) {
        Command *command = &pipeline->commands[pipeline->count - 1];
        Token *token = &tokens->items[index];
        if (token->type == TOKEN_WORD) {
            if (add_argument(command, token->text) != 0) {
                set_error(error_message, "out of memory");
                return -1;
            }
            token->text = NULL;
            continue;
        }
        if (token->type == TOKEN_PIPE) {
            if (command->argc == 0 || index + 1 == tokens->count ||
                tokens->items[index + 1].type == TOKEN_PIPE ||
                tokens->items[index + 1].type == TOKEN_BACKGROUND) {
                set_error(error_message, "missing command around '|'");
                return -1;
            }
            if (add_command(pipeline) != 0) {
                set_error(error_message, "out of memory");
                return -1;
            }
            continue;
        }
        if (token->type == TOKEN_BACKGROUND) {
            if (index + 1 != tokens->count) {
                set_error(error_message, "'&' must appear at the end");
                return -1;
            }
            pipeline->background = true;
            continue;
        }
        if (index + 1 >= tokens->count ||
            tokens->items[index + 1].type != TOKEN_WORD) {
            set_error(error_message, "redirection requires a file path");
            return -1;
        }
        Token *path = &tokens->items[++index];
        if (token->type == TOKEN_INPUT) {
            if (command->input_path != NULL) {
                set_error(error_message, "multiple input redirections");
                return -1;
            }
            command->input_path = path->text;
        } else {
            if (command->output_path != NULL) {
                set_error(error_message, "multiple output redirections");
                return -1;
            }
            command->output_path = path->text;
            command->append_output = token->type == TOKEN_APPEND;
        }
        path->text = NULL;
    }

    for (size_t index = 0; index < pipeline->count; index++) {
        if (pipeline->commands[index].argc == 0) {
            set_error(error_message, "redirection without a command");
            return -1;
        }
    }
    pipeline->display = strdup(line);
    if (pipeline->display == NULL) {
        set_error(error_message, "out of memory");
        return -1;
    }
    size_t display_length = strlen(pipeline->display);
    while (display_length > 0 &&
           (pipeline->display[display_length - 1] == '\n' ||
            pipeline->display[display_length - 1] == '\r')) {
        pipeline->display[--display_length] = '\0';
    }
    return 1;
}

int parse_line(const char *line, Pipeline *pipeline, char **error_message)
{
    if (error_message != NULL) {
        *error_message = NULL;
    }
    TokenList tokens = {0};
    int result = tokenize(line, &tokens, error_message);
    if (result == 0) {
        result = parse_tokens(&tokens, pipeline, line, error_message);
    }
    free_tokens(&tokens);
    if (result < 0) {
        free_pipeline(pipeline);
    }
    return result;
}

void free_pipeline(Pipeline *pipeline)
{
    for (size_t index = 0; index < pipeline->count; index++) {
        Command *command = &pipeline->commands[index];
        for (size_t argument = 0; argument < command->argc; argument++) {
            free(command->argv[argument]);
        }
        free(command->argv);
        free(command->input_path);
        free(command->output_path);
    }
    free(pipeline->commands);
    free(pipeline->display);
    *pipeline = (Pipeline){0};
}