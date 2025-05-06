const express = require('express');
const router = express.Router();
const jwt = require('jsonwebtoken');
const Game = require('../models/Game');
const User = require('../models/User');

// Hardcoded JWT Secret
const JWT_SECRET = 'your_jwt_secret_key';

// Middleware to authenticate token
const auth = async (req, res, next) => {
  try {
    const token = req.header('Authorization')?.replace('Bearer ', '');
    
    if (!token) {
      return res.status(401).json({ message: 'Authentication required' });
    }
    
    const decoded = jwt.verify(token, JWT_SECRET);
    const user = await User.findById(decoded.userId);
    
    if (!user) {
      return res.status(401).json({ message: 'Invalid token' });
    }
    
    req.user = user;
    next();
  } catch (error) {
    res.status(401).json({ message: 'Authentication failed' });
  }
};

// Save a game record
router.post('/', auth, async (req, res) => {
  try {
    const { result, opponent, moves, duration, gameType } = req.body;
    
    const game = new Game({
      player: req.user._id,
      result,
      opponent: opponent || 'Computer',
      moves: moves || [],
      duration: duration || 0,
      gameType: gameType || 'tictactoe'
    });
    
    await game.save();
    
    res.status(201).json(game);
  } catch (error) {
    res.status(500).json({ message: error.message });
  }
});

// Get user's game history
router.get('/history', auth, async (req, res) => {
  try {
    const page = parseInt(req.query.page) || 1;
    const limit = parseInt(req.query.limit) || 10;
    const skip = (page - 1) * limit;
    
    const games = await Game.find({ player: req.user._id })
      .sort({ createdAt: -1 })
      .skip(skip)
      .limit(limit);
      
    const total = await Game.countDocuments({ player: req.user._id });
    
    res.json({
      games,
      pagination: {
        total,
        page,
        pages: Math.ceil(total / limit)
      }
    });
  } catch (error) {
    res.status(500).json({ message: error.message });
  }
});

// Get game details by ID
router.get('/:id', auth, async (req, res) => {
  try {
    const game = await Game.findOne({ 
      _id: req.params.id,
      player: req.user._id
    });
    
    if (!game) {
      return res.status(404).json({ message: 'Game not found' });
    }
    
    res.json(game);
  } catch (error) {
    res.status(500).json({ message: error.message });
  }
});

// Get user statistics
router.get('/stats/summary', auth, async (req, res) => {
  try {
    const gameStats = await Game.aggregate([
      { $match: { player: req.user._id } },
      { $group: {
          _id: '$result',
          count: { $sum: 1 }
        }
      }
    ]);
    
    // Format the results
    const stats = {
      total: 0,
      wins: 0,
      losses: 0,
      draws: 0
    };
    
    gameStats.forEach(stat => {
      stats[stat._id + 's'] = stat.count;
      stats.total += stat.count;
    });
    
    stats.winRate = stats.total > 0 ? Math.round((stats.wins / stats.total) * 100) : 0;
    
    res.json(stats);
  } catch (error) {
    res.status(500).json({ message: error.message });
  }
});

module.exports = router; 