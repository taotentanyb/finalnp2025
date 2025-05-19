# Tic-Tac-Toe Network Game

A multiplayer Tic-Tac-Toe game with LAN/WAN connectivity, chat functionality, and spectator mode.

## Features

- Play Tic-Tac-Toe over network with another player
- Connect via LAN (local network) or WAN (internet)
- Chat with your opponent during gameplay
- Spectate ongoing games
- Automatic server discovery via GitHub repository
- Clean disconnection handling and game statistics

## Requirements

### Client
- C compiler (gcc recommended)
- libcurl development files (`libcurl4-openssl-dev`)
- Network connection

### Server
- C compiler (gcc recommended)
- pthread library
- ngrok for WAN connectivity
- GitHub account (optional, for automatic server info publishing)

## Compilation

Compile both the client and server programs:

```bash
# Compile the server
gcc server.c -o server -pthread

# Compile the client
sudo apt-get install libcurl4-openssl-dev
gcc client.c -o client -lcurl
```

## Server Setup

1. Start the server:
   ```bash
   ./server
   ```

2. The server will automatically start ngrok for WAN connectivity and display connection information.
   - If successful, the server will save connection details to `ngrok_info.txt`
   - If GitHub token is configured, it will also update this information in the GitHub repository

3. For automatic GitHub updates (optional):
   - Create a `.github_token` file with your GitHub personal access token
   - Ensure the token has permissions to update repository content

## Client Usage

### Connecting to the Server

1. Start the client:
   ```bash
   ./client
   ```

2. Choose a connection type:
   - Option 1: LAN connection (same local network)
   - Option 2: WAN connection (through internet)

3. For WAN connection, the client will try these methods to find the server:
   - Read from local `ngrok_info.txt` file
   - Download server info from GitHub repository
   - Accept manual input of server address and port

### Gameplay Instructions

- When it's your turn, you can:
  - Enter a number 1-9 to place your mark on the board:
    ```
     1 | 2 | 3 
    ---|---|---
     4 | 5 | 6 
    ---|---|---
     7 | 8 | 9 
    ```
  - Send chat messages with the format: `chat Your message here`
  - Type `quit` to leave the game
  - Type `help` to see instructions

- When spectating:
  - View the list of active games
  - Enter a game number to watch
  - Type `refresh` to update the game list
  - Type `quit` to disconnect

## WAN Connection Tutorial

Follow these steps to connect to the server over the internet:

1. Start the client by running `./client`

2. When prompted, select option 2 for WAN connection:
   ```
   Chọn loại kết nối:
   1. Kết nối LAN (cùng mạng local)
   2. Kết nối WAN (qua Internet)
   Lựa chọn của bạn (1/2): 2
   ```

3. The client will automatically try to find the server:
   - First by checking for a local `ngrok_info.txt` file
   - Then by downloading the server info from the GitHub repository

4. If the automatic methods fail, you will be prompted to enter the server details manually:
   ```
   Nhập host ngrok (ví dụ: 0.tcp.ap.ngrok.io): 
   ```
   Enter the ngrok hostname (e.g., `0.tcp.ap.ngrok.io`)
   
   ```
   Nhập port ngrok: 
   ```
   Enter the port number (e.g., `12345`)

5. Once connected, you can choose to play or spectate:
   ```
   WELCOME: Enter 'play' to join a game or 'spectate' to watch:
   ```

6. If you choose to play, you'll be matched with another player or wait for an opponent
   - If you're the first player, you'll play as X
   - If you're the second player, you'll play as O

7. During gameplay, use the number pad 1-9 to mark positions on the board

8. To chat with your opponent, type: `chat Your message here`

9. To disconnect at any time, type: `quit`

## Troubleshooting

- **Connection Issues:** 
  - Ensure the server is running and ngrok is active
  - Check that the GitHub repository has the latest ngrok information
  - Verify your internet connection

- **Compilation Errors:**
  - Install required libraries: `sudo apt-get install libcurl4-openssl-dev`

- **Server Not Found:**
  - Ask the server operator for the current ngrok address and port
  - Try manually entering the connection details when prompted

## License

This project is licensed under the MIT License
