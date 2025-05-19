#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h> // For threading
#include <signal.h>  // For signal handling
#include <sys/types.h> // For process functions

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_PENDING_CONNECTIONS 10 // How many can queue up for accept()
#define MAX_SPECTATORS 5 // Maximum number of spectators per game
#define NGROK_PORT_FILE "ngrok_info.txt"
#define GITHUB_REPO "taotentanyb/finalnp2025"
#define GITHUB_TOKEN_FILE ".github_token"

// Ngrok process ID
pid_t ngrok_pid = -1;

// Game board representation (shared by game logic, not globally for threads)
char player_symbols[2] = {'X', 'O'};

// --- Global data for matchmaking (protected by mutex) ---
pthread_mutex_t matchmaking_mutex = PTHREAD_MUTEX_INITIALIZER;
int waiting_player_socket = -1;
char waiting_player_ip[INET_ADDRSTRLEN];
int waiting_player_port;

// --- Global statistics (protected by mutex) ---
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
int total_games_played = 0;
int x_wins = 0;
int o_wins = 0;
int draws = 0;
// Additional statistics
int total_moves_made = 0;
int average_game_length = 0;
int longest_game_moves = 0;
int shortest_game_moves = 999;

// Function prototypes
void update_github_file();
void cleanup();

// --- Struct to pass arguments to the game thread ---
typedef struct {
    int player_sockets[2];
    char player_ips[2][INET_ADDRSTRLEN];
    int player_ports[2];
    char local_board[3][3]; // Each game thread has its own board
    // Spectator support
    int spectator_sockets[MAX_SPECTATORS];
    char spectator_ips[MAX_SPECTATORS][INET_ADDRSTRLEN];
    int spectator_ports[MAX_SPECTATORS];
    int spectator_count;
    pthread_mutex_t spectator_mutex;
} game_session_args_t;


// --- Game Logic Helper Functions (operate on game_session_args_t's local_board) ---
void initialize_board_local(game_session_args_t* game_args) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            game_args->local_board[i][j] = ' ';
        }
    }
}

void format_board_string_local(game_session_args_t* game_args, char* buffer) {
    // Note: The buffer passed here is 'board_str' which is BUFFER_SIZE.
    // This function ensures its own output fits within that.
    snprintf(buffer, BUFFER_SIZE, "BOARD:\n %c | %c | %c \n---|---|---\n %c | %c | %c \n---|---|---\n %c | %c | %c \n",
             game_args->local_board[0][0], game_args->local_board[0][1], game_args->local_board[0][2],
             game_args->local_board[1][0], game_args->local_board[1][1], game_args->local_board[1][2],
             game_args->local_board[2][0], game_args->local_board[2][1], game_args->local_board[2][2]);
}

int check_win_local(game_session_args_t* game_args, char player_symbol) {
    for (int i = 0; i < 3; i++) {
        if ((game_args->local_board[i][0] == player_symbol && game_args->local_board[i][1] == player_symbol && game_args->local_board[i][2] == player_symbol) ||
            (game_args->local_board[0][i] == player_symbol && game_args->local_board[1][i] == player_symbol && game_args->local_board[2][i] == player_symbol)) {
            return 1;
        }
    }
    if ((game_args->local_board[0][0] == player_symbol && game_args->local_board[1][1] == player_symbol && game_args->local_board[2][2] == player_symbol) ||
        (game_args->local_board[0][2] == player_symbol && game_args->local_board[1][1] == player_symbol && game_args->local_board[2][0] == player_symbol)) {
        return 1;
    }
    return 0;
}

int check_draw_local(game_session_args_t* game_args) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (game_args->local_board[i][j] == ' ') {
                return 0;
            }
        }
    }
    return 1;
}

int make_move_local(game_session_args_t* game_args, int move, char player_symbol) {
    if (move < 1 || move > 9) return 0;
    int row = (move - 1) / 3;
    int col = (move - 1) % 3;
    if (game_args->local_board[row][col] == ' ') {
        game_args->local_board[row][col] = player_symbol;
        return 1;
    }
    return 0; // Cell already taken
}

// --- New Function: Send update to all spectators ---
void update_spectators(game_session_args_t* game_args, char* message) {
    pthread_mutex_lock(&game_args->spectator_mutex);
    for (int i = 0; i < game_args->spectator_count; i++) {
        if (game_args->spectator_sockets[i] > 0) {
            if (send(game_args->spectator_sockets[i], message, strlen(message), 0) < 0) {
                printf("INFO: Spectator %s:%d disconnected.\n", 
                       game_args->spectator_ips[i], 
                       game_args->spectator_ports[i]);
                close(game_args->spectator_sockets[i]);
                
                // Remove this spectator by shifting the array
                for (int j = i; j < game_args->spectator_count - 1; j++) {
                    game_args->spectator_sockets[j] = game_args->spectator_sockets[j+1];
                    strcpy(game_args->spectator_ips[j], game_args->spectator_ips[j+1]);
                    game_args->spectator_ports[j] = game_args->spectator_ports[j+1];
                }
                game_args->spectator_count--;
                i--; // Adjust counter since we removed an element
            }
        }
    }
    pthread_mutex_unlock(&game_args->spectator_mutex);
}

// --- New Function: Add a spectator to a game ---
int add_spectator(game_session_args_t* game_args, int socket, char* ip, int port) {
    pthread_mutex_lock(&game_args->spectator_mutex);
    if (game_args->spectator_count >= MAX_SPECTATORS) {
        pthread_mutex_unlock(&game_args->spectator_mutex);
        return 0; // Can't add more spectators
    }
    
    int idx = game_args->spectator_count;
    game_args->spectator_sockets[idx] = socket;
    strcpy(game_args->spectator_ips[idx], ip);
    game_args->spectator_ports[idx] = port;
    game_args->spectator_count++;
    
    pthread_mutex_unlock(&game_args->spectator_mutex);
    return 1; // Successfully added
}

// --- Main Game Playing Function (runs in a separate thread per game) ---
void play_tic_tac_toe(game_session_args_t* game_args) {
    char buffer[BUFFER_SIZE]; // For receiving client input
    char board_str[BUFFER_SIZE]; // For the formatted board string
    // *** MODIFICATION HERE: Increased size for message_to_send ***
    char message_to_send[BUFFER_SIZE + 256]; // For composing messages to send
    char spectator_message[BUFFER_SIZE + 256]; // For messages to spectators

    int current_player_idx = 0; // Player 0 ('X') starts
    int game_over = 0;
    int turn_count = 0;
    int move_count = 0; // Track number of moves for statistics
    ssize_t bytes_received;

    initialize_board_local(game_args);
    printf("INFO: Game started between %s:%d (X) and %s:%d (O)\n",
           game_args->player_ips[0], game_args->player_ports[0],
           game_args->player_ips[1], game_args->player_ports[1]);

    // Inform players of their symbols and game start
    // Using sizeof(message_to_send) for all snprintf to message_to_send
    snprintf(message_to_send, sizeof(message_to_send), "INFO: Game Start! You are Player %c.\n", player_symbols[0]);
    send(game_args->player_sockets[0], message_to_send, strlen(message_to_send), 0);
    snprintf(message_to_send, sizeof(message_to_send), "INFO: Game Start! You are Player %c.\n", player_symbols[1]);
    send(game_args->player_sockets[1], message_to_send, strlen(message_to_send), 0);

    // Inform spectators of game start
    snprintf(spectator_message, sizeof(spectator_message), 
             "INFO: You are spectating a game between %s (X) and %s (O).\n",
             game_args->player_ips[0], game_args->player_ips[1]);
    update_spectators(game_args, spectator_message);

    while (!game_over) {
        int current_socket = game_args->player_sockets[current_player_idx];
        int opponent_socket = game_args->player_sockets[1 - current_player_idx];
        char current_symbol = player_symbols[current_player_idx];
        char opponent_symbol = player_symbols[1 - current_player_idx];

        format_board_string_local(game_args, board_str); // board_str is BUFFER_SIZE
        // Send board (board_str is already a complete message here)
        send(game_args->player_sockets[0], board_str, strlen(board_str), 0);
        send(game_args->player_sockets[1], board_str, strlen(board_str), 0);
        update_spectators(game_args, board_str); // Send board to spectators

        snprintf(message_to_send, sizeof(message_to_send), "YOUR_TURN: Player %c, enter move (1-9) or 'chat <message>': \n", current_symbol);
        send(current_socket, message_to_send, strlen(message_to_send), 0);
        snprintf(message_to_send, sizeof(message_to_send), "WAIT: Player %c's (%s) turn.\n", current_symbol, game_args->player_ips[current_player_idx]);
        send(opponent_socket, message_to_send, strlen(message_to_send), 0);
        
        // Send turn information to spectators
        snprintf(spectator_message, sizeof(spectator_message), 
                 "SPECTATE: Player %c's (%s) turn.\n", 
                 current_symbol, game_args->player_ips[current_player_idx]);
        update_spectators(game_args, spectator_message);

        printf("INFO: Player %c's turn (%s:%d).\n", current_symbol, game_args->player_ips[current_player_idx], game_args->player_ports[current_player_idx]);

        int turn_action_taken = 0;
        while (!turn_action_taken && !game_over) {
            memset(buffer, 0, BUFFER_SIZE);
            bytes_received = recv(current_socket, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_received <= 0) {
                printf("INFO: Player %c (%s:%d) disconnected.\n", current_symbol, game_args->player_ips[current_player_idx], game_args->player_ports[current_player_idx]);
                snprintf(message_to_send, sizeof(message_to_send), "GAME_OVER: OPPONENT_DISCONNECTED: Player %c disconnected. You win by default.\n", current_symbol);
                send(opponent_socket, message_to_send, strlen(message_to_send), 0);
                
                // Inform spectators
                snprintf(spectator_message, sizeof(spectator_message), 
                         "GAME_OVER: Player %c disconnected. Player %c wins by default.\n", 
                         current_symbol, opponent_symbol);
                update_spectators(game_args, spectator_message);
                
                game_over = 1;
                pthread_mutex_lock(&stats_mutex);
                total_games_played++;
                if (opponent_symbol == 'X') x_wins++; else o_wins++;
                // Update move statistics
                total_moves_made += move_count;
                average_game_length = total_games_played > 0 ? total_moves_made / total_games_played : 0;
                pthread_mutex_unlock(&stats_mutex);
                break;
            }
            buffer[bytes_received] = '\0';
            buffer[strcspn(buffer, "\n\r")] = 0;

            printf("DEBUG: Player %c (%s:%d) sent: '%s'\n", current_symbol, game_args->player_ips[current_player_idx], game_args->player_ports[current_player_idx], buffer);

            if (strncmp(buffer, "chat ", 5) == 0) {
                if (strlen(buffer + 5) > 0) {
                    char chat_msg_to_opponent[BUFFER_SIZE]; // Can use regular BUFFER_SIZE for this
                    snprintf(chat_msg_to_opponent, sizeof(chat_msg_to_opponent), "CHAT_MSG: [Player %c]: %s\n", current_symbol, buffer + 5);
                    send(opponent_socket, chat_msg_to_opponent, strlen(chat_msg_to_opponent), 0);
                    
                    // Send chat to spectators
                    snprintf(spectator_message, sizeof(spectator_message), 
                             "CHAT_MSG: [Player %c]: %s\n", current_symbol, buffer + 5);
                    update_spectators(game_args, spectator_message);
                    
                    snprintf(message_to_send, sizeof(message_to_send), "INFO: Chat sent. YOUR_TURN: Player %c, enter move (1-9) or 'chat <message>': \n", current_symbol);
                    send(current_socket, message_to_send, strlen(message_to_send), 0);
                } else {
                    snprintf(message_to_send, sizeof(message_to_send), "INVALID_INPUT: Empty chat message. YOUR_TURN: Player %c, enter move (1-9) or 'chat <message>': \n", current_symbol);
                    send(current_socket, message_to_send, strlen(message_to_send), 0);
                }
            } else {
                int move_choice;
                if (sscanf(buffer, "%d", &move_choice) == 1) {
                    if (make_move_local(game_args, move_choice, current_symbol)) {
                        turn_count++;
                        move_count++; // Increment move count for statistics
                        turn_action_taken = 1;
                        snprintf(message_to_send, sizeof(message_to_send), "INFO: Move %d accepted.\n", move_choice);
                        send(current_socket, message_to_send, strlen(message_to_send), 0);
                        
                        // Inform spectators of move
                        snprintf(spectator_message, sizeof(spectator_message), 
                                 "INFO: Player %c made move %d.\n", 
                                 current_symbol, move_choice);
                        update_spectators(game_args, spectator_message);
                    } else {
                        snprintf(message_to_send, sizeof(message_to_send), "INVALID_MOVE: Cell taken or invalid number. YOUR_TURN: Player %c, enter move (1-9) or 'chat <message>': \n", current_symbol);
                        send(current_socket, message_to_send, strlen(message_to_send), 0);
                    }
                } else {
                    snprintf(message_to_send, sizeof(message_to_send), "INVALID_INPUT: Enter a number (1-9) or 'chat <message>'. YOUR_TURN: Player %c, enter move (1-9) or 'chat <message>': \n", current_symbol);
                    send(current_socket, message_to_send, strlen(message_to_send), 0);
                }
            }
        }

        if (game_over) break;

        if (check_win_local(game_args, current_symbol)) {
            format_board_string_local(game_args, board_str);
            pthread_mutex_lock(&stats_mutex);
            total_games_played++;
            if (current_symbol == 'X') x_wins++; else o_wins++;
            
            // Update move statistics
            total_moves_made += move_count;
            if (move_count > longest_game_moves) longest_game_moves = move_count;
            if (move_count < shortest_game_moves) shortest_game_moves = move_count;
            average_game_length = total_games_played > 0 ? total_moves_made / total_games_played : 0;
            
            // *** MODIFICATION HERE: Using sizeof(message_to_send) ***
            snprintf(message_to_send, sizeof(message_to_send), 
                     "GAME_OVER: WINNER: Player %c wins!\n%s\nSTATS: Games: %d, X Wins: %d, O Wins: %d, Draws: %d\nGame Length: %d moves, Avg: %d, Min: %d, Max: %d\n",
                     current_symbol, board_str, total_games_played, x_wins, o_wins, draws,
                     move_count, average_game_length, 
                     shortest_game_moves != 999 ? shortest_game_moves : move_count, 
                     longest_game_moves);
            pthread_mutex_unlock(&stats_mutex);
            game_over = 1;
        } else if (turn_count == 9) {
             if (check_draw_local(game_args)) {
                format_board_string_local(game_args, board_str);
                pthread_mutex_lock(&stats_mutex);
                total_games_played++;
                draws++;
                
                // Update move statistics
                total_moves_made += move_count;
                if (move_count > longest_game_moves) longest_game_moves = move_count;
                if (move_count < shortest_game_moves) shortest_game_moves = move_count;
                average_game_length = total_games_played > 0 ? total_moves_made / total_games_played : 0;
                
                // *** MODIFICATION HERE: Using sizeof(message_to_send) ***
                snprintf(message_to_send, sizeof(message_to_send), 
                         "GAME_OVER: DRAW: It's a draw!\n%s\nSTATS: Games: %d, X Wins: %d, O Wins: %d, Draws: %d\nGame Length: %d moves, Avg: %d, Min: %d, Max: %d\n",
                         board_str, total_games_played, x_wins, o_wins, draws,
                         move_count, average_game_length, 
                         shortest_game_moves != 999 ? shortest_game_moves : move_count, 
                         longest_game_moves);
                pthread_mutex_unlock(&stats_mutex);
                game_over = 1;
            }
        }

        if (game_over) {
            send(game_args->player_sockets[0], message_to_send, strlen(message_to_send), 0);
            send(game_args->player_sockets[1], message_to_send, strlen(message_to_send), 0);
            update_spectators(game_args, message_to_send); // Send final result to spectators
            printf("INFO: Game ended. Result recorded.\n");
        } else {
            current_player_idx = 1 - current_player_idx;
        }
    }
    printf("INFO: Game thread finished for %s:%d and %s:%d.\n",
           game_args->player_ips[0], game_args->player_ports[0],
           game_args->player_ips[1], game_args->player_ports[1]);
}

// --- Thread function to handle a game session ---
void* game_thread_function(void* arg_ptr) {
    game_session_args_t* args = (game_session_args_t*)arg_ptr;

    play_tic_tac_toe(args);

    printf("INFO: Closing connections for game between %s:%d and %s:%d\n",
           args->player_ips[0], args->player_ports[0],
           args->player_ips[1], args->player_ports[1]);
           
    // Close player connections
    close(args->player_sockets[0]);
    close(args->player_sockets[1]);
    
    // Close spectator connections
    pthread_mutex_lock(&args->spectator_mutex);
    for (int i = 0; i < args->spectator_count; i++) {
        close(args->spectator_sockets[i]);
    }
    pthread_mutex_unlock(&args->spectator_mutex);
    
    pthread_mutex_destroy(&args->spectator_mutex);
    free(args);
    pthread_detach(pthread_self());
    return NULL;
}

// --- Global structure to store active game sessions ---
typedef struct {
    game_session_args_t* games[MAX_PENDING_CONNECTIONS];
    int game_count;
    pthread_mutex_t games_mutex;
} active_games_t;

active_games_t active_games = {
    .game_count = 0,
    .games_mutex = PTHREAD_MUTEX_INITIALIZER
};

// --- Add a game session to the active games list ---
void add_game_session(game_session_args_t* game) {
    pthread_mutex_lock(&active_games.games_mutex);
    if (active_games.game_count < MAX_PENDING_CONNECTIONS) {
        active_games.games[active_games.game_count++] = game;
    }
    pthread_mutex_unlock(&active_games.games_mutex);
}

// --- Remove a game session from the active games list ---
void remove_game_session(game_session_args_t* game) {
    pthread_mutex_lock(&active_games.games_mutex);
    for (int i = 0; i < active_games.game_count; i++) {
        if (active_games.games[i] == game) {
            // Remove by shifting elements
            for (int j = i; j < active_games.game_count - 1; j++) {
                active_games.games[j] = active_games.games[j + 1];
            }
            active_games.game_count--;
            break;
        }
    }
    pthread_mutex_unlock(&active_games.games_mutex);
}

// --- Get a list of active games for spectator choice ---
void list_active_games(char* buffer, size_t size) {
    pthread_mutex_lock(&active_games.games_mutex);
    snprintf(buffer, size, "ACTIVE_GAMES: %d games in progress\n", active_games.game_count);
    for (int i = 0; i < active_games.game_count; i++) {
        char game_info[256];
        snprintf(game_info, sizeof(game_info), "Game %d: %s (X) vs %s (O) - Spectators: %d/%d\n", 
                 i + 1, 
                 active_games.games[i]->player_ips[0], 
                 active_games.games[i]->player_ips[1],
                 active_games.games[i]->spectator_count,
                 MAX_SPECTATORS);
        strncat(buffer, game_info, size - strlen(buffer) - 1);
    }
    pthread_mutex_unlock(&active_games.games_mutex);
}

// --- Thread function to handle a spectator connection ---
void* spectator_thread_function(void* socket_ptr) {
    int client_socket = *((int*)socket_ptr);
    free(socket_ptr);
    
    char buffer[BUFFER_SIZE];
    char game_list[BUFFER_SIZE * 2];
    
    // Send welcome message and list of games
    snprintf(buffer, sizeof(buffer), "WELCOME: You are in spectator mode. Choose a game to watch:\n");
    send(client_socket, buffer, strlen(buffer), 0);
    
    list_active_games(game_list, sizeof(game_list));
    send(client_socket, game_list, strlen(game_list), 0);
    
    snprintf(buffer, sizeof(buffer), "Enter game number to spectate (1-%d) or 'refresh' to update the list: ", 
             active_games.game_count > 0 ? active_games.game_count : 1);
    send(client_socket, buffer, strlen(buffer), 0);
    
    int joined_game = 0;
    while (!joined_game) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            break; // Client disconnected
        }
        
        buffer[bytes_received] = '\0';
        buffer[strcspn(buffer, "\n\r")] = 0;
        
        if (strncmp(buffer, "refresh", 7) == 0) {
            list_active_games(game_list, sizeof(game_list));
            send(client_socket, game_list, strlen(game_list), 0);
            snprintf(buffer, sizeof(buffer), "Enter game number to spectate (1-%d) or 'refresh' to update the list: ", 
                     active_games.game_count > 0 ? active_games.game_count : 1);
            send(client_socket, buffer, strlen(buffer), 0);
        } else {
            int game_choice;
            if (sscanf(buffer, "%d", &game_choice) == 1 && 
                game_choice >= 1 && 
                game_choice <= active_games.game_count) {
                
                pthread_mutex_lock(&active_games.games_mutex);
                game_session_args_t* chosen_game = active_games.games[game_choice - 1];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                
                // Get client IP and port for tracking
                getpeername(client_socket, (struct sockaddr*)&client_addr, &addr_len);
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                int client_port = ntohs(client_addr.sin_port);
                
                // Try to add as spectator
                if (add_spectator(chosen_game, client_socket, client_ip, client_port)) {
                    snprintf(buffer, sizeof(buffer), "INFO: Successfully joined game %d as spectator.\n", game_choice);
                    send(client_socket, buffer, strlen(buffer), 0);
                    joined_game = 1;
                    pthread_mutex_unlock(&active_games.games_mutex);
                    
                    // The game thread will now handle this socket
                    pthread_detach(pthread_self());
                    return NULL;
                } else {
                    snprintf(buffer, sizeof(buffer), "ERROR: Game %d has reached maximum spectator capacity.\n", game_choice);
                    send(client_socket, buffer, strlen(buffer), 0);
                    pthread_mutex_unlock(&active_games.games_mutex);
                    
                    // Send updated list
                    list_active_games(game_list, sizeof(game_list));
                    send(client_socket, game_list, strlen(game_list), 0);
                    
                    snprintf(buffer, sizeof(buffer), "Enter game number to spectate (1-%d) or 'refresh' to update the list: ", 
                             active_games.game_count > 0 ? active_games.game_count : 1);
                    send(client_socket, buffer, strlen(buffer), 0);
                }
            } else {
                snprintf(buffer, sizeof(buffer), "INVALID_INPUT: Please enter a valid game number or 'refresh'.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                
                list_active_games(game_list, sizeof(game_list));
                send(client_socket, game_list, strlen(game_list), 0);
                
                snprintf(buffer, sizeof(buffer), "Enter game number to spectate (1-%d) or 'refresh' to update the list: ", 
                         active_games.game_count > 0 ? active_games.game_count : 1);
                send(client_socket, buffer, strlen(buffer), 0);
            }
        }
    }
    
    // If we get here, close the connection
    close(client_socket);
    pthread_detach(pthread_self());
    return NULL;
}

// Function to start ngrok and extract the port
void start_ngrok() {
    printf("Starting ngrok tunnel...\n");
    
    // Find ngrok path
    FILE *which_ngrok = popen("which ngrok", "r");
    char ngrok_path[256] = "/usr/local/bin/ngrok"; // Default path
    
    if (which_ngrok) {
        char found_path[256];
        if (fgets(found_path, sizeof(found_path), which_ngrok)) {
            found_path[strcspn(found_path, "\n")] = 0; // Remove newline
            strcpy(ngrok_path, found_path);
        }
        pclose(which_ngrok);
    }
    
    printf("Using ngrok at: %s\n", ngrok_path);
    
    // Kill any existing ngrok processes
    system("pkill -f ngrok");
    sleep(1);
    
    // Start ngrok with output redirection
    char ngrok_cmd[512];
    sprintf(ngrok_cmd, "%s tcp 8080 > ngrok_output.txt 2>&1 &", ngrok_path);
    system(ngrok_cmd);
    
    printf("Started ngrok in background. Waiting for tunnel...\n");
    sleep(10); // Wait for ngrok to start
    
    // Try different methods to get tunnel info
    int found = 0;
    char host[128] = "";
    int port = 0;
    
    // Method 1: Check ngrok API
    printf("Trying to get tunnel info from ngrok API...\n");
    FILE *api_output = popen("curl -s http://127.0.0.1:4040/api/tunnels", "r");
    if (api_output) {
        char api_data[2048] = {0};
        fread(api_data, 1, sizeof(api_data) - 1, api_output);
        pclose(api_output);
        
        printf("API output: %s\n", api_data);
        
        // Parse simple JSON to find public_url
        char *public_url = strstr(api_data, "public_url");
        if (public_url) {
            char *url_start = strstr(public_url, "tcp://");
            if (url_start) {
                url_start += 6; // Skip "tcp://"
                char *colon = strchr(url_start, ':');
                if (colon) {
                    // Extract host
                    int host_len = colon - url_start;
                    strncpy(host, url_start, host_len);
                    host[host_len] = '\0';
                    
                    // Extract port
                    char *port_start = colon + 1;
                    char *port_end = strchr(port_start, '"');
                    if (port_end) {
                        char port_str[10] = {0};
                        strncpy(port_str, port_start, port_end - port_start);
                        port = atoi(port_str);
                        found = 1;
                    }
                }
            }
        }
    }
    
    // Method 2: Check the output file if API failed
    if (!found) {
        printf("API method failed. Checking output file...\n");
        FILE *fp = fopen("ngrok_output.txt", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                printf("Read line: %s", line);
                
                if (strstr(line, "tcp://") != NULL) {
                    // Example: "Forwarding                    tcp://0.tcp.ap.ngrok.io:12345 -> localhost:8080"
                    char *start = strstr(line, "tcp://");
                    if (start) {
                        start += 6; // Skip "tcp://"
                        char *colon = strchr(start, ':');
                        if (colon) {
                            // Extract host
                            int host_len = colon - start;
                            strncpy(host, start, host_len);
                            host[host_len] = '\0';
                            
                            // Extract port
                            char *port_start = colon + 1;
                            char *arrow = strstr(port_start, "->");
                            if (arrow) {
                                char port_str[10] = {0};
                                strncpy(port_str, port_start, arrow - port_start - 1);
                                port = atoi(port_str);
                                found = 1;
                            } else {
                                port = atoi(port_start);
                                found = (port > 0);
                            }
                        }
                    }
                    if (found) break;
                }
            }
            fclose(fp);
        }
    }
    
    // Method 3: Run ngrok status command
    if (!found) {
        printf("Output file method failed. Running ngrok status command...\n");
        FILE *status_output = popen("ngrok status", "r");
        if (status_output) {
            char line[256];
            while (fgets(line, sizeof(line), status_output)) {
                printf("Status line: %s", line);
                
                if (strstr(line, "tcp://") != NULL) {
                    char *start = strstr(line, "tcp://");
                    if (start) {
                        start += 6; // Skip "tcp://"
                        char *colon = strchr(start, ':');
                        if (colon) {
                            // Extract host
                            int host_len = colon - start;
                            strncpy(host, start, host_len);
                            host[host_len] = '\0';
                            
                            // Extract port - handle both "12345 ->" and "12345" formats
                            char *port_start = colon + 1;
                            char *arrow = strstr(port_start, "->");
                            if (arrow) {
                                char port_str[10] = {0};
                                strncpy(port_str, port_start, arrow - port_start - 1);
                                port = atoi(port_str);
                                found = 1;
                            } else {
                                char port_str[10] = {0};
                                int i = 0;
                                while (*port_start >= '0' && *port_start <= '9' && i < 9) {
                                    port_str[i++] = *port_start++;
                                }
                                port = atoi(port_str);
                                found = (port > 0);
                            }
                        }
                    }
                    if (found) break;
                }
            }
            pclose(status_output);
        }
    }
    
    // Method 4: Direct inspection
    if (!found) {
        printf("All automatic methods failed. Running direct inspection...\n");
        // Get tunnel info by running ngrok in a way we can easily capture output
        FILE *direct_output = popen("ngrok tcp 8080 --log=stdout", "r");
        if (direct_output) {
            char line[256];
            printf("Waiting for direct ngrok output...\n");
            int timeout = 30; // 30 seconds timeout
            while (timeout-- > 0 && !found && fgets(line, sizeof(line), direct_output)) {
                printf("Direct: %s", line);
                
                if (strstr(line, "tcp://") != NULL) {
                    char *start = strstr(line, "tcp://");
                    if (start) {
                        start += 6; // Skip "tcp://"
                        char *colon = strchr(start, ':');
                        if (colon) {
                            // Extract host
                            int host_len = colon - start;
                            strncpy(host, start, host_len);
                            host[host_len] = '\0';
                            
                            // Extract port
                            char *port_start = colon + 1;
                            char port_str[10] = {0};
                            int i = 0;
                            while (*port_start >= '0' && *port_start <= '9' && i < 9) {
                                port_str[i++] = *port_start++;
                            }
                            port = atoi(port_str);
                            found = (port > 0);
                            
                            // Kill the direct process and restart ngrok in background
                            pclose(direct_output);
                            system("pkill -f ngrok");
                            sleep(1);
                            system(ngrok_cmd);
                            break;
                        }
                    }
                }
                
                // Every second check
                if (timeout % 3 == 0) {
                    sleep(1);
                }
            }
            if (!found) {
                pclose(direct_output);
            }
        }
    }
    
    // If we found the tunnel info, save it
    if (found) {
        printf("Ngrok tunnel established: %s:%d\n", host, port);
        
        // Save the ngrok info to a file
        FILE *info_file = fopen(NGROK_PORT_FILE, "w");
        if (info_file) {
            fprintf(info_file, "%s %d", host, port);
            fclose(info_file);
            printf("Ngrok info saved to %s\n", NGROK_PORT_FILE);
            
            // Push the updated file to GitHub
            update_github_file();
        }
    } else {
        printf("All methods failed to extract ngrok tunnel info.\n");
        printf("Try manually running 'ngrok tcp 8080' in another terminal\n");
        printf("and then create %s manually with format: hostname port\n", NGROK_PORT_FILE);
    }
}

// Function to update the ngrok info file on GitHub
void update_github_file() {
    printf("Updating ngrok info on GitHub...\n");
    
    // Read GitHub token from file
    FILE *token_file = fopen(GITHUB_TOKEN_FILE, "r");
    if (!token_file) {
        printf("GitHub token file not found. Skipping GitHub update.\n");
        return;
    }
    
    char token[256];
    if (fgets(token, sizeof(token), token_file) == NULL) {
        printf("Failed to read GitHub token. Skipping GitHub update.\n");
        fclose(token_file);
        return;
    }
    fclose(token_file);
    
    // Remove newline character if present
    token[strcspn(token, "\n")] = 0;
    
    // Create a temporary file for the request data
    FILE *temp = fopen("github_update.json", "w");
    if (!temp) {
        perror("Failed to create temporary file");
        return;
    }
    
    // Read the current ngrok info
    FILE *ngrok_file = fopen(NGROK_PORT_FILE, "r");
    if (!ngrok_file) {
        perror("Failed to open ngrok info file");
        fclose(temp);
        return;
    }
    
    char ngrok_info[256];
    if (fgets(ngrok_info, sizeof(ngrok_info), ngrok_file) == NULL) {
        perror("Failed to read ngrok info");
        fclose(ngrok_file);
        fclose(temp);
        return;
    }
    fclose(ngrok_file);
    
    // Remove newline character if present
    ngrok_info[strcspn(ngrok_info, "\n")] = 0;
    
    // Create JSON data for GitHub API
    // Note: We're using bash and curl to avoid JSON encoding issues
    fprintf(temp, "{\n");
    fprintf(temp, "  \"message\": \"Update ngrok connection info\",\n");
    fprintf(temp, "  \"content\": \"");
    
    // Base64 encode the content
    char cmd[512];
    sprintf(cmd, "echo -n \"%s\" | base64 | tr -d '\\n'", ngrok_info);
    FILE *base64_pipe = popen(cmd, "r");
    if (!base64_pipe) {
        perror("Failed to encode content");
        fclose(temp);
        return;
    }
    
    char base64[512];
    if (fgets(base64, sizeof(base64), base64_pipe) == NULL) {
        perror("Failed to read encoded content");
        pclose(base64_pipe);
        fclose(temp);
        return;
    }
    pclose(base64_pipe);
    
    fprintf(temp, "%s\"\n", base64);
    
    // Check if file already exists to get the SHA
    fprintf(temp, "}\n");
    fclose(temp);
    
    // Use curl to update the file on GitHub
    char curl_cmd[1024];
    sprintf(curl_cmd, 
            "curl -s -X PUT -H \"Authorization: token %s\" "
            "-H \"Accept: application/vnd.github.v3+json\" "
            "-d @github_update.json "
            "https://api.github.com/repos/%s/contents/%s", 
            token, GITHUB_REPO, NGROK_PORT_FILE);
    
    system(curl_cmd);
    printf("GitHub update request sent.\n");
    
    // Clean up temporary file
    remove("github_update.json");
}

// Clean up resources when server terminates
void cleanup() {
    if (ngrok_pid > 0) {
        printf("Terminating ngrok process (PID: %d)...\n", ngrok_pid);
        kill(ngrok_pid, SIGTERM);
        sleep(1); // Give ngrok time to terminate gracefully
    }
    printf("Server shutdown complete.\n");
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char client_addr_str[INET_ADDRSTRLEN];
    
    // Set up signal handler to clean up resources on exit
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    
    // Start ngrok first to get the tunnel ready
    start_ngrok();
    
    // Initialize active games tracking
    active_games_t games;
    games.game_count = 0;
    pthread_mutex_init(&games.games_mutex, NULL);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);
    printf("Tic-Tac-Toe Server  - Now with spectator mode and enhanced statistics\n");

    while (1) {
        printf("INFO: Waiting for new connections...\n");
        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) < 0) {
            perror("accept failed");
            continue;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, client_addr_str, INET_ADDRSTRLEN);
        int client_port_val = ntohs(client_addr.sin_port);
        printf("INFO: Connection accepted from %s:%d\n", client_addr_str, client_port_val);

        // Ask the client if they want to play or spectate
        char msg_choice[BUFFER_SIZE];
        snprintf(msg_choice, sizeof(msg_choice), 
                 "WELCOME: Enter 'play' to join a game or 'spectate' to watch: ");
        send(new_socket, msg_choice, strlen(msg_choice), 0);
        
        char choice_buffer[BUFFER_SIZE] = {0};
        int bytes_received = recv(new_socket, choice_buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            close(new_socket);
            continue;
        }
        
        choice_buffer[bytes_received] = '\0';
        choice_buffer[strcspn(choice_buffer, "\n\r")] = 0;
        
        if (strncmp(choice_buffer, "spectate", 8) == 0) {
            // Create thread to handle spectator
            int* sock_ptr = malloc(sizeof(int));
            if (!sock_ptr) {
                perror("Failed to allocate memory for spectator socket");
                close(new_socket);
                continue;
            }
            *sock_ptr = new_socket;
            
            pthread_t spectator_tid;
            if (pthread_create(&spectator_tid, NULL, spectator_thread_function, sock_ptr) != 0) {
                perror("pthread_create failed for spectator thread");
                free(sock_ptr);
                close(new_socket);
            }
        } else {
            // Default to player behavior
            pthread_mutex_lock(&matchmaking_mutex);
            if (waiting_player_socket == -1) {
                waiting_player_socket = new_socket;
                strcpy(waiting_player_ip, client_addr_str);
                waiting_player_port = client_port_val;
                pthread_mutex_unlock(&matchmaking_mutex);

                char msg_wait[BUFFER_SIZE]; // Regular buffer size is fine for this short message
                snprintf(msg_wait, sizeof(msg_wait), "INFO: You are Player X. Waiting for an opponent...\n");
                send(new_socket, msg_wait, strlen(msg_wait), 0);
                printf("INFO: Player X (%s:%d) is waiting.\n", client_addr_str, client_port_val);
            } else {
                game_session_args_t *args = malloc(sizeof(game_session_args_t));
                if (!args) {
                    perror("Failed to allocate memory for game session args");
                    close(new_socket);
                    pthread_mutex_unlock(&matchmaking_mutex);
                    continue;
                }

                args->player_sockets[0] = waiting_player_socket;
                strcpy(args->player_ips[0], waiting_player_ip);
                args->player_ports[0] = waiting_player_port;

                args->player_sockets[1] = new_socket;
                strcpy(args->player_ips[1], client_addr_str);
                args->player_ports[1] = client_port_val;
                
                // Initialize spectator fields
                args->spectator_count = 0;
                for (int i = 0; i < MAX_SPECTATORS; i++) {
                    args->spectator_sockets[i] = -1;
                }
                pthread_mutex_init(&args->spectator_mutex, NULL);

                waiting_player_socket = -1;
                pthread_mutex_unlock(&matchmaking_mutex);

                printf("INFO: Pairing Player X (%s:%d) with Player O (%s:%d).\n",
                       args->player_ips[0], args->player_ports[0],
                       args->player_ips[1], args->player_ports[1]);
                
                // Add this game to the active games list
                add_game_session(args);

                pthread_t game_tid;
                if (pthread_create(&game_tid, NULL, game_thread_function, args) != 0) {
                    perror("pthread_create failed for game thread");
                    close(args->player_sockets[0]);
                    close(args->player_sockets[1]);
                    remove_game_session(args);
                    free(args);
                }
            }
        }
    }

    close(server_fd);
    return 0;
}