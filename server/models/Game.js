const mongoose = require('mongoose');

const gameSchema = new mongoose.Schema({
  players: [{
    type: mongoose.Schema.Types.ObjectId,
    ref: 'User'
  }],
  result: {
    type: String,
    enum: ['player1_win', 'player2_win', 'draw'],
    required: true
  },
  moves: [{
    player: {
      type: Number, // 1 or 2 for player1 or player2
      required: true
    },
    position: {
      type: Number, // 0-8 for the cell position
      required: true
    },
    timestamp: {
      type: Date,
      default: Date.now
    }
  }],
  duration: {
    type: Number, // in seconds
    default: 0
  },
  isAgainstAI: {
    type: Boolean,
    default: false
  },
  aiDifficulty: {
    type: String,
    enum: ['easy', 'medium', 'hard', ''],
    default: ''
  }
}, {
  timestamps: true
});

const Game = mongoose.model('Game', gameSchema);

module.exports = Game; 