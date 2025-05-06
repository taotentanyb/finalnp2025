import socket
import json
import threading
import time
from matchmaking_client import MatchmakingClient

class TicTacToeGame:
    def __init__(self):
        self.board = [''] * 9
        self.current_player = 'X'
        self.game_active = True
        self.my_symbol = None
        self.opponent = None
        self.match_id = None
        self.game_socket = None
        self.receive_thread = None
    
    def set_match_info(self, match_id, opponent, my_symbol):
        """Set the match information"""
        self.match_id = match_id
        self.opponent = opponent
        self.my_symbol = my_symbol
        print(f"Game started against {opponent['username']}")
        print(f"You are playing as: {my_symbol}")
    
    def display_board(self):
        """Display the current game board"""
        board_display = ""
        for i in range(0, 9, 3):
            row = []
            for j in range(3):
                cell = self.board[i+j] if self.board[i+j] else ' '
                row.append(cell)
            board_display += f" {row[0]} | {row[1]} | {row[2]} \n"
            if i < 6:
                board_display += "---+---+---\n"
        print(board_display)
    
    def make_move(self, position):
        """Make a move on the board"""
        if not self.game_active:
            return False
            
        if position < 0 or position > 8:
            print("Invalid position. Choose between 0-8.")
            return False
            
        if self.board[position]:
            print("Position already taken.")
            return False
            
        if self.current_player != self.my_symbol:
            print("Not your turn.")
            return False
            
        # Update the board
        self.board[position] = self.my_symbol
        
        # Send the move to the opponent
        self.send_game_data({
            'action': 'move',
            'position': position
        })
        
        # Check for game over
        self.check_game_status()
        
        # Switch player
        self.current_player = 'O' if self.current_player == 'X' else 'X'
        
        return True
    
    def receive_move(self, position):
        """Receive opponent's move"""
        opponent_symbol = 'O' if self.my_symbol == 'X' else 'X'
        self.board[position] = opponent_symbol
        print(f"Opponent placed {opponent_symbol} at position {position}")
        
        # Check for game over
        self.check_game_status()
        
        # Switch player
        self.current_player = self.my_symbol
        
        self.display_board()
    
    def check_game_status(self):
        """Check if the game is over"""
        # Winning combinations
        winning_combinations = [
            [0, 1, 2], [3, 4, 5], [6, 7, 8],  # rows
            [0, 3, 6], [1, 4, 7], [2, 5, 8],  # columns
            [0, 4, 8], [2, 4, 6]              # diagonals
        ]
        
        # Check for a win
        for combo in winning_combinations:
            a, b, c = combo
            if self.board[a] and self.board[a] == self.board[b] == self.board[c]:
                winner = self.board[a]
                if winner == self.my_symbol:
                    print("You win!")
                else:
                    print("You lose!")
                self.game_active = False
                return
        
        # Check for a draw
        if '' not in self.board:
            print("Game ended in a draw!")
            self.game_active = False
    
    def connect_to_game_server(self, host, port):
        """Connect to a direct game server (for P2P communication)"""
        try:
            self.game_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.game_socket.connect((host, port))
            
            # Start receive thread
            self.receive_thread = threading.Thread(target=self.receive_game_messages)
            self.receive_thread.daemon = True
            self.receive_thread.start()
            
            return True
        except Exception as e:
            print(f"Failed to connect to game server: {e}")
            return False
    
    def receive_game_messages(self):
        """Receive and process game messages"""
        while self.game_active and self.game_socket:
            try:
                data = self.game_socket.recv(4096).decode().strip()
                if not data:
                    break
                
                message = json.loads(data)
                self.handle_game_message(message)
            
            except json.JSONDecodeError:
                print("Received invalid game data")
            except Exception as e:
                print(f"Error receiving game data: {e}")
                break
    
    def handle_game_message(self, message):
        """Handle game-specific messages"""
        action = message.get('action')
        
        if action == 'move':
            position = message.get('position')
            self.receive_move(position)
        
        elif action == 'game_over':
            reason = message.get('reason', 'unknown')
            print(f"Game over: {reason}")
            self.game_active = False
    
    def send_game_data(self, data):
        """Send game data"""
        if not self.game_socket:
            return False
        
        try:
            message = json.dumps(data)
            self.game_socket.send(f"{message}\n".encode())
            return True
        except Exception as e:
            print(f"Error sending game data: {e}")
            return False

class GameClient(MatchmakingClient):
    def __init__(self, host='localhost', port=5000):
        super().__init__(host, port)
        self.game = TicTacToeGame()
        self.game_server_host = 'localhost'
        self.game_server_port = 5001
    
    def handle_message(self, message):
        """Override handle_message to start the game when match is found"""
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
            
            # Start the game
            self.start_game(match_id, opponent)
        
        elif action == 'matchmaking_cancelled':
            self.matchmaking = False
            print("Matchmaking has been cancelled")
    
    def start_game(self, match_id, opponent):
        """Start the game with the matched opponent"""
        # Connect to game server
        if self.game.connect_to_game_server(self.game_server_host, self.game_server_port):
            # In a real implementation, we might negotiate who plays as X and O
            # For this example, the first player alphabetically is X
            my_symbol = 'X' if self.socket.getsockname()[0] < opponent.get('username', '') else 'O'
            
            # Initialize the game
            self.game.set_match_info(match_id, opponent, my_symbol)
            self.game.display_board()
            
            # If player is X, they go first
            if my_symbol == 'X':
                print("You go first!")
            else:
                print("Opponent goes first!")
        else:
            print("Failed to initialize the game. Try again.")
            self.match_found = False

def main():
    client = GameClient()
    
    # Connect to matchmaking server
    username = input("Enter your username: ")
    if not client.connect(username):
        return
    
    # Main client loop
    try:
        while True:
            if client.match_found and client.game.game_active:
                # Game is active, show game menu
                print("\nGame Options:")
                print("1-9. Make a move (positions 0-8)")
                print("0. Exit game")
                
                choice = input("Select an option: ")
                
                if choice == '0':
                    client.game.game_active = False
                    client.match_found = False
                    print("Exiting game...")
                elif choice.isdigit() and 0 <= int(choice) <= 8:
                    client.game.make_move(int(choice))
                    client.game.display_board()
                else:
                    print("Invalid option")
            else:
                # No active game, show matchmaking menu
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
                    waiting_dots = 0
                    while client.matchmaking and not client.match_found and client.connected:
                        waiting_message = "Waiting for a match" + "." * waiting_dots
                        print(waiting_message, end="\r")
                        waiting_dots = (waiting_dots + 1) % 4
                        time.sleep(0.5)
                    print(" " * 25, end="\r")  # Clear the waiting message
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()

if __name__ == "__main__":
    main() 