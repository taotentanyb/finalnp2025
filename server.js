const express = require('express');
const mongoose = require('mongoose');
const cors = require('cors');
const path = require('path');
require('dotenv').config();
const WebSocket = require('ws');
const http = require('http');
const { v4: uuidv4 } = require('uuid');

// Import routes
const userRoutes = require('./server/routes/users');
const gameRoutes = require('./server/routes/games');

const app = express();

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, '/')));

// Routes
app.use('/api/users', userRoutes);
app.use('/api/games', gameRoutes);

// Serve static files
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'login.html'));
});

app.get('/tiktaktoe.html', (req, res) => {
  res.sendFile(path.join(__dirname, 'tiktaktoe.html'));
});

app.get('/profile.html', (req, res) => {
  res.sendFile(path.join(__dirname, 'profile.html'));
});

// Tạo HTTP server
const server = http.createServer(app);

// Khởi tạo WebSocket server
const wss = new WebSocket.Server({ server });

// Store clients and matchmaking queue
const clients = new Map();
const waitingPlayers = [];
const matches = new Map();

// WebSocket connection handler
wss.on('connection', (ws) => {
    const clientId = uuidv4();
    const clientInfo = {
        id: clientId,
        ws: ws,
        username: 'Guest',
        isInQueue: false,
        currentMatch: null
    };
    
    clients.set(clientId, clientInfo);
    console.log(`New connection: ${clientId}`);

    // Handle incoming messages
    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            handleMessage(clientInfo, data);
        } catch (error) {
            console.error('Error parsing message:', error);
        }
    });

    // Handle disconnection
    ws.on('close', () => {
        handleDisconnect(clientId);
    });
});

// Message handler
function handleMessage(client, data) {
    const action = data.action;
    
    switch (action) {
        case 'player_info':
            client.username = data.username || `Guest_${client.id.substr(0, 5)}`;
            console.log(`Player ${client.username} registered`);
            break;
            
        case 'find_match':
            findMatch(client);
            break;
            
        case 'cancel_matchmaking':
            cancelMatchmaking(client);
            break;
            
        case 'move':
            handleGameMove(client, data);
            break;
            
        case 'game_over':
            handleGameOver(client, data);
            break;
    }
}

// Find a match for a player
function findMatch(client) {
    if (client.isInQueue) {
        return;
    }
    
    console.log(`${client.username} is looking for a match`);
    
    // Add to waiting list
    client.isInQueue = true;
    waitingPlayers.push(client);
    
    // Notify client they're in queue
    sendToClient(client, {
        action: 'waiting_for_match'
    });
    
    // Try to match players
    matchPlayers();
}

// Match players in the queue
function matchPlayers() {
    while (waitingPlayers.length >= 2) {
        const player1 = waitingPlayers.shift();
        const player2 = waitingPlayers.shift();
        
        // Create a match
        const matchId = uuidv4();
        const match = {
            id: matchId,
            players: [player1.id, player2.id],
            board: ['', '', '', '', '', '', '', '', '']
        };
        
        matches.set(matchId, match);
        
        // Update players
        player1.isInQueue = false;
        player2.isInQueue = false;
        player1.currentMatch = matchId;
        player2.currentMatch = matchId;
        
        // Notify players about the match
        sendToClient(player1, {
            action: 'match_found',
            match_id: matchId,
            symbol: 'X',
            opponent: {
                username: player2.username
            }
        });
        
        sendToClient(player2, {
            action: 'match_found',
            match_id: matchId,
            symbol: 'O',
            opponent: {
                username: player1.username
            }
        });
        
        console.log(`Match created: ${player1.username} vs ${player2.username}`);
    }
}

// Cancel matchmaking for a player
function cancelMatchmaking(client) {
    if (!client.isInQueue) {
        return;
    }
    
    // Remove from waiting list
    const index = waitingPlayers.findIndex(p => p.id === client.id);
    if (index !== -1) {
        waitingPlayers.splice(index, 1);
    }
    
    client.isInQueue = false;
    
    // Notify client
    sendToClient(client, {
        action: 'matchmaking_cancelled'
    });
    
    console.log(`${client.username} cancelled matchmaking`);
}

// Handle game moves
function handleGameMove(client, data) {
    if (!client.currentMatch) {
        return;
    }
    
    const match = matches.get(client.currentMatch);
    if (!match) {
        return;
    }
    
    // Find opponent
    const opponentId = match.players.find(id => id !== client.id);
    if (!opponentId) {
        return;
    }
    
    const opponent = clients.get(opponentId);
    if (!opponent) {
        return;
    }
    
    // Forward move to opponent
    sendToClient(opponent, {
        action: 'move',
        position: data.position
    });
}

// Handle game over
function handleGameOver(client, data) {
    if (!client.currentMatch) {
        return;
    }
    
    const match = matches.get(client.currentMatch);
    if (!match) {
        return;
    }
    
    // Clean up the match
    for (const playerId of match.players) {
        const player = clients.get(playerId);
        if (player && player.id !== client.id) {
            player.currentMatch = null;
            sendToClient(player, {
                action: 'game_over',
                result: data.result === 'win' ? 'lose' : data.result
            });
        }
    }
    
    client.currentMatch = null;
    matches.delete(match.id);
}

// Handle client disconnection
function handleDisconnect(clientId) {
    const client = clients.get(clientId);
    if (!client) {
        return;
    }
    
    console.log(`Client disconnected: ${client.username}`);
    
    // If in matchmaking queue, remove
    if (client.isInQueue) {
        const index = waitingPlayers.findIndex(p => p.id === clientId);
        if (index !== -1) {
            waitingPlayers.splice(index, 1);
        }
    }
    
    // If in a match, notify opponent
    if (client.currentMatch) {
        const match = matches.get(client.currentMatch);
        if (match) {
            for (const playerId of match.players) {
                if (playerId !== clientId) {
                    const opponent = clients.get(playerId);
                    if (opponent) {
                        opponent.currentMatch = null;
                        sendToClient(opponent, {
                            action: 'game_over',
                            reason: 'opponent_disconnected'
                        });
                    }
                }
            }
            matches.delete(client.currentMatch);
        }
    }
    
    // Remove client
    clients.delete(clientId);
}

// Utility function to send data to a client
function sendToClient(client, data) {
    if (client.ws.readyState === WebSocket.OPEN) {
        client.ws.send(JSON.stringify(data));
    }
}

// Try to connect with local MongoDB first
const LOCAL_MONGODB_URI = 'mongodb://localhost:27017/gameportal';
// Fallback to MongoDB Atlas URI (you can replace this with your own connection string if you have one)
const ATLAS_MONGODB_URI = 'mongodb+srv://demo:demo123@cluster0.mongodb.net/gameportal?retryWrites=true&w=majority';
const PORT = process.env.PORT || 5000;

// First try local connection
mongoose.connect(LOCAL_MONGODB_URI)
  .then(() => {
    console.log('Connected to local MongoDB');
    startServer();
  })
  .catch(localErr => {
    console.log('Failed to connect to local MongoDB, trying Atlas...');
    
    // Fallback to Atlas if local fails
    mongoose.connect(ATLAS_MONGODB_URI)
      .then(() => {
        console.log('Connected to MongoDB Atlas');
        startServer();
      })
      .catch(atlasErr => {
        console.error('Failed to connect to MongoDB Atlas', atlasErr);
        
        // Handle fallback to in-memory "DB" with limited functionality
        console.log('Starting server without database connection');
        console.log('Note: User registration and game history will not be saved');
        startServer();
      });
  });

function startServer() {
  server.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
  });
} 