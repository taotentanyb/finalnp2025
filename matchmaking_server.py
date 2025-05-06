import socket
import threading
import json
import time

class MatchmakingServer:
    def __init__(self, host='0.0.0.0', port=5000):
        self.host = host
        self.port = port
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.clients = []
        self.waiting_players = []
        self.lock = threading.Lock()
        
    def start(self):
        self.server_socket.listen(5)
        print(f"Matchmaking server started on {self.host}:{self.port}")
        
        try:
            while True:
                client_socket, address = self.server_socket.accept()
                print(f"New connection from {address}")
                
                client_thread = threading.Thread(target=self.handle_client, args=(client_socket, address))
                client_thread.daemon = True
                client_thread.start()
        except KeyboardInterrupt:
            print("Server shutting down...")
        finally:
            self.server_socket.close()
    
    def handle_client(self, client_socket, address):
        try:
            # Get player information
            player_info = self.receive_data(client_socket)
            if not player_info:
                return
            
            player = {
                'socket': client_socket,
                'address': address,
                'username': player_info.get('username', f"Player_{address[0]}_{address[1]}"),
                'rating': player_info.get('rating', 1000)
            }
            
            with self.lock:
                self.clients.append(player)
            
            # Main client loop
            while True:
                data = self.receive_data(client_socket)
                if not data:
                    break
                
                action = data.get('action')
                
                if action == 'find_match':
                    self.find_match(player)
                elif action == 'cancel_matchmaking':
                    self.cancel_matchmaking(player)
                elif action == 'disconnect':
                    break
        
        except Exception as e:
            print(f"Error handling client {address}: {e}")
        finally:
            self.disconnect_client(client_socket, address)
    
    def find_match(self, player):
        print(f"Player {player['username']} is looking for a match")
        
        # Add player to waiting list
        with self.lock:
            if player not in self.waiting_players:
                self.waiting_players.append(player)
            
            # Try to find a match
            if len(self.waiting_players) >= 2:
                player1 = self.waiting_players.pop(0)
                player2 = self.waiting_players.pop(0)
                
                # Create a match
                match_id = f"match_{int(time.time())}_{player1['username']}_{player2['username']}"
                
                # Notify players
                match_data = {
                    'action': 'match_found',
                    'match_id': match_id,
                    'opponent': {
                        'username': player2['username'],
                        'rating': player2['rating']
                    }
                }
                self.send_data(player1['socket'], match_data)
                
                match_data = {
                    'action': 'match_found',
                    'match_id': match_id,
                    'opponent': {
                        'username': player1['username'],
                        'rating': player1['rating']
                    }
                }
                self.send_data(player2['socket'], match_data)
                
                print(f"Match created: {player1['username']} vs {player2['username']}")
            else:
                # Notify player they're in queue
                self.send_data(player['socket'], {'action': 'waiting_for_match'})
    
    def cancel_matchmaking(self, player):
        with self.lock:
            if player in self.waiting_players:
                self.waiting_players.remove(player)
                self.send_data(player['socket'], {'action': 'matchmaking_cancelled'})
                print(f"Player {player['username']} cancelled matchmaking")
    
    def disconnect_client(self, client_socket, address):
        with self.lock:
            # Find and remove client from lists
            for client in self.clients:
                if client['socket'] == client_socket:
                    self.clients.remove(client)
                    if client in self.waiting_players:
                        self.waiting_players.remove(client)
                    break
        
        client_socket.close()
        print(f"Client {address} disconnected")
    
    def send_data(self, client_socket, data):
        try:
            message = json.dumps(data)
            client_socket.send(f"{message}\n".encode())
        except Exception as e:
            print(f"Error sending data: {e}")
    
    def receive_data(self, client_socket):
        try:
            data = client_socket.recv(4096).decode().strip()
            if not data:
                return None
            return json.loads(data)
        except json.JSONDecodeError:
            print("Received invalid JSON data")
            return None
        except Exception as e:
            print(f"Error receiving data: {e}")
            return None

if __name__ == "__main__":
    server = MatchmakingServer()
    server.start()
