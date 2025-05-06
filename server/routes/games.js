const express = require('express');
const router = express.Router();
const jwt = require('jsonwebtoken');
const Game = require('../models/Game');
const User = require('../models/User');

// JWT Secret
const JWT_SECRET = process.env.JWT_SECRET || 'your_jwt_secret_key';

// Helper functions
const getUserModel = () => {
  return global.User || User;
};

const getGameModel = () => {
  return global.Game || Game;
};

// Authentication middleware
const authenticate = async (req, res, next) => {
  try {
    const token = req.headers.authorization?.split(' ')[1];
    
    if (!token) {
      return res.status(401).json({ message: 'No token, authorization denied' });
    }
    
    try {
      // Verify token
      const decoded = jwt.verify(token, JWT_SECRET);
      const UserModel = getUserModel();
      
      // Get user
      const user = await UserModel.findOne({ _id: decoded.userId });
      
      if (!user) {
        return res.status(401).json({ message: 'Invalid token' });
      }
      
      req.user = user;
      next();
    } catch (err) {
      return res.status(401).json({ message: 'Token is not valid' });
    }
  } catch (error) {
    console.error('Auth error:', error);
    res.status(500).json({ message: 'Server error' });
  }
};

// Save a new game record
router.post('/', authenticate, async (req, res) => {
  try {
    const { result, moves, duration, isAgainstAI, aiDifficulty, opponentId } = req.body;
    const GameModel = getGameModel();
    const UserModel = getUserModel();
    
    // Create game record
    const players = [req.user._id];
    if (opponentId && !isAgainstAI) {
      players.push(opponentId);
    }
    
    const game = await GameModel.create({
      players,
      result,
      moves,
      duration,
      isAgainstAI,
      aiDifficulty: aiDifficulty || ''
    });
    
    // Update user stats
    if (req.user.gameStats) {
      if (result === 'player1_win') {
        req.user.gameStats.wins += 1;
      } else if (result === 'player2_win') {
        req.user.gameStats.losses += 1;
      } else {
        req.user.gameStats.draws += 1;
      }
      
      await UserModel.updateOne(
        { _id: req.user._id },
        { 
          $set: { gameStats: req.user.gameStats },
          $inc: { 'gameStats.totalGames': 1 }
        }
      );
    }
    
    res.status(201).json({ game });
  } catch (error) {
    console.error('Save game error:', error);
    res.status(500).json({ message: 'Server error saving game' });
  }
});

// Get user's game history
router.get('/history', authenticate, async (req, res) => {
  try {
    const GameModel = getGameModel();
    
    // Get games where user is a player
    const games = await GameModel.find({ 
      players: req.user._id 
    }).sort({ createdAt: -1 }).limit(10);
    
    res.json({ games });
  } catch (error) {
    console.error('Game history error:', error);
    res.status(500).json({ message: 'Server error retrieving game history' });
  }
});

// Get user's game statistics summary
router.get('/stats/summary', authenticate, async (req, res) => {
  try {
    res.json({ 
      stats: req.user.gameStats,
      winRate: req.user.winRate 
    });
  } catch (error) {
    console.error('Game stats error:', error);
    res.status(500).json({ message: 'Server error retrieving game stats' });
  }
});

module.exports = router; 