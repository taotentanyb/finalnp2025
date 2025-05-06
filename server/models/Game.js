const mongoose = require('mongoose');

const GameSchema = new mongoose.Schema({
  gameType: {
    type: String,
    enum: ['tictactoe', 'other'],
    default: 'tictactoe'
  },
  player: {
    type: mongoose.Schema.Types.ObjectId,
    ref: 'User',
    required: true
  },
  opponent: {
    type: String,
    default: 'Computer'
  },
  result: {
    type: String,
    enum: ['win', 'loss', 'draw'],
    required: true
  },
  moves: {
    type: Array,
    default: []
  },
  duration: {
    type: Number, // In seconds
    default: 0
  },
  createdAt: {
    type: Date,
    default: Date.now
  }
});

module.exports = mongoose.model('Game', GameSchema); 