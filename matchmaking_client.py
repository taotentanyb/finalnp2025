import socket
import json
import threading
import time

class MatchmakingClient:
    def __init__(self, host='localhost', port=5000):
        self.host = host
        self.port = port
        self.socket = None
        self.connected = False
        self.matchmaking = False
        self.match_found = False
        self.match_data = None
        self.receive_thread = None
        
    def connect(self, username, rating=1000):
        """Connect to the matchmaking server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            self.connected = True
            
            # Send player info
            player_info = {
                'username': username,
                'rating': rating
            }
            self.send_data(player_info)
            
            # Start receive thread
            self.receive_thread = threading.Thread(target=self.receive_messages)
            self.receive_thread.daemon = True
            self.receive_thread.start()
            
            print(f"Connected to matchmaking server at {self.host}:{self.port}")
            return True
        
        except Exception as e:
            print(f"Failed to connect to server: {e}")
            self.connected = False
            return False
    
    def find_match(self):
        """Request to find a match"""
        if not self.connected:
            print("Not connected to server")
            return False
        
        self.matchmaking = True
        self.send_data({'action': 'find_match'})
        print("Looking for a match...")
        return True
    
    def cancel_matchmaking(self):
        """Cancel the matchmaking request"""
        if not self.connected or not self.matchmaking:
            return False
        
        self.send_data({'action': 'cancel_matchmaking'})
        self.matchmaking = False
        print("Matchmaking cancelled")
        return True
    
    def disconnect(self):
        """Disconnect from the server"""
        if not self.connected:
            return
        
        try:
            self.send_data({'action': 'disconnect'})
            self.socket.close()
        except:
            pass
        finally:
            self.connected = False
            self.matchmaking = False
            print("Disconnected from server")
    
    def send_data(self, data):
        """Send data to the server"""
        if not self.connected:
            return False
        
        try:
            message = json.dumps(data)
            self.socket.send(f"{message}\n".encode())
            return True
        except Exception as e:
            print(f"Error sending data: {e}")
            self.connected = False
            return False
    
    def receive_messages(self):
        """Receive and process messages from the server"""
        while self.connected:
            try:
                data = self.socket.recv(4096).decode().strip()
                if not data:
                    self.connected = False
                    print("Disconnected from server")
                    break
                
                message = json.loads(data)
                self.handle_message(message)
            
            except json.JSONDecodeError:
                print("Received invalid data from server")
            except Exception as e:
                print(f"Error receiving data: {e}")
                self.connected = False
                break
    
    def handle_message(self, message):
        """Handle messages from the server"""
        action = message.get('action')
        
        if action == 'waiting_for_match':
            print("Waiting for an opponent...")
        
        elif action == 'match_found':
            self.match_found = True
            self.matchmaking = False
            self.match_data = message
            
            opponent = message.get('opponent', {})
            match_id = message.get('match_id', 'unknown')
            
            print(f"Match found! Playing against {opponent.get('username')} (Rating: {opponent.get('rating')})")
            print(f"Match ID: {match_id}")
            
            # Here you would typically start the game
            # For example: self.start_game(match_id, opponent)
        
        elif action == 'matchmaking_cancelled':
            self.matchmaking = False
            print("Matchmaking has been cancelled")

# Example usage
def main():
    client = MatchmakingClient()
    
    # Connect to server
    username = input("Enter your username: ")
    if not client.connect(username):
        return
    
    # Main client loop
    try:
        while True:
            print("\nOptions:")
            print("1. Find match")
            print("2. Cancel matchmaking")
            print("3. Disconnect")
            
            choice = input("Select an option (1-3): ")
            
            if choice == '1':
                client.find_match()
            elif choice == '2':
                client.cancel_matchmaking()
            elif choice == '3':
                client.disconnect()
                break
            else:
                print("Invalid option")
            
            # Wait until match is found or action is cancelled
            if client.matchmaking:
                while client.matchmaking and not client.match_found and client.connected:
                    time.sleep(0.5)
                
                if client.match_found:
                    # In a real game, you would start the game here
                    # For this example, we'll just wait and then reset
                    time.sleep(3)
                    client.match_found = False
                    print("Game finished. Ready for a new match.")
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()

if __name__ == "__main__":
    main() 