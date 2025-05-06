import socket
import threading
import json
import time

class GameServer:
    def __init__(self, host='0.0.0.0', port=5001):
        self.host = host
        self.port = port
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.clients = []
        self.matches = {}  # match_id -> [player1, player2]
        self.lock = threading.Lock()
    
    def start(self):
        self.server_socket.listen(5)
        print(f"Game server started on {self.host}:{self.port}")
        
        try:
            while True:
                client_socket, address = self.server_socket.accept()
                print(f"New game connection from {address}")
                
                client_thread = threading.Thread(target=self.handle_client, args=(client_socket, address))
                client_thread.daemon = True
                client_thread.start()
        except KeyboardInterrupt:
            print("Game server shutting down...")
        finally:
            self.server_socket.close()
    
    def handle_client(self, client_socket, address):
        try:
            # First message should contain player info and match ID
            player_info = self.receive_data(client_socket)
            if not player_info:
                return
            
            match_id = player_info.get('match_id')
            username = player_info.get('username')
            
            player = {
                'socket': client_socket,
                'address': address,
                'username': username,
                'match_id': match_id
            }
            
            with self.lock:
                self.clients.append(player)
                
                # Add to match or create new match
                if match_id not in self.matches:
                    self.matches[match_id] = [player]
                    # Let player know they're waiting for opponent
                    self.send_data(client_socket, {
                        'action': 'waiting_for_opponent',
                        'match_id': match_id
                    })
                else:
                    # Second player joined, both players are ready
                    self.matches[match_id].append(player)
                    
                    # Start the game by notifying both players
                    for p in self.matches[match_id]:
                        self.send_data(p['socket'], {
                            'action': 'game_start',
                            'match_id': match_id,
                            'players': [p2['username'] for p2 in self.matches[match_id]]
                        })
            
            # Main client loop
            while True:
                data = self.receive_data(client_socket)
                if not data:
                    break
                
                # Forward game actions to the opponent
                self.handle_game_action(player, data)
        
        except Exception as e:
            print(f"Error handling game client {address}: {e}")
        finally:
            self.disconnect_client(client_socket, address)
    
    def handle_game_action(self, player, data):
        """Forward game actions to opponent"""
        match_id = player['match_id']
        
        with self.lock:
            if match_id in self.matches:
                # Find opponent
                for opponent in self.matches[match_id]:
                    if opponent['socket'] != player['socket']:
                        # Forward the action
                        self.send_data(opponent['socket'], data)
                        break
    
    def disconnect_client(self, client_socket, address):
        """Handle client disconnection"""
        with self.lock:
            # Find player
            player_to_remove = None
            for player in self.clients:
                if player['socket'] == client_socket:
                    player_to_remove = player
                    break
            
            if player_to_remove:
                match_id = player_to_remove['match_id']
                
                # Remove from clients list
                self.clients.remove(player_to_remove)
                
                # Notify opponent that player left
                if match_id in self.matches:
                    match_players = self.matches[match_id]
                    
                    # Remove player from match
                    if player_to_remove in match_players:
                        match_players.remove(player_to_remove)
                    
                    # Notify remaining players
                    for remaining_player in match_players:
                        self.send_data(remaining_player['socket'], {
                            'action': 'game_over',
                            'reason': 'opponent_disconnected'
                        })
                    
                    # If no players left, remove the match
                    if not match_players:
                        del self.matches[match_id]
        
        client_socket.close()
        print(f"Game client {address} disconnected")
    
    def send_data(self, client_socket, data):
        """Send data to a client"""
        try:
            message = json.dumps(data)
            client_socket.send(f"{message}\n".encode())
        except Exception as e:
            print(f"Error sending game data: {e}")
    
    def receive_data(self, client_socket):
        """Receive data from a client"""
        try:
            data = client_socket.recv(4096).decode().strip()
            if not data:
                return None
            return json.loads(data)
        except json.JSONDecodeError:
            print("Received invalid JSON data")
            return None
        except Exception as e:
            print(f"Error receiving game data: {e}")
            return None

if __name__ == "__main__":
    server = GameServer()
    server.start() 