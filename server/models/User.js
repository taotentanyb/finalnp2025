const mongoose = require('mongoose');
const bcrypt = require('bcryptjs');

const userSchema = new mongoose.Schema({
  username: {
    type: String,
    required: true,
    unique: true,
    trim: true,
    minlength: 3
  },
  email: {
    type: String,
    required: true,
    unique: true,
    trim: true,
    lowercase: true
  },
  password: {
    type: String,
    required: true,
    minlength: 6
  },
  gameStats: {
    wins: {
      type: Number,
      default: 0
    },
    losses: {
      type: Number,
      default: 0
    },
    draws: {
      type: Number,
      default: 0
    },
    totalGames: {
      type: Number,
      default: 0
    }
  }
}, {
  timestamps: true
});

// Virtual for winRate
userSchema.virtual('winRate').get(function() {
  if (this.gameStats.totalGames === 0) return 0;
  return (this.gameStats.wins / this.gameStats.totalGames) * 100;
});

// Pre-save hook to hash password
userSchema.pre('save', async function(next) {
  if (!this.isModified('password')) return next();
  
  try {
    const salt = await bcrypt.genSalt(10);
    this.password = await bcrypt.hash(this.password, salt);
    next();
  } catch (error) {
    next(error);
  }
});

// Method to compare password
userSchema.methods.comparePassword = async function(candidatePassword) {
  return await bcrypt.compare(candidatePassword, this.password);
};

// Update totalGames when other stats change
userSchema.pre('save', function(next) {
  if (this.isModified('gameStats.wins') || 
      this.isModified('gameStats.losses') || 
      this.isModified('gameStats.draws')) {
    this.gameStats.totalGames = 
      this.gameStats.wins + this.gameStats.losses + this.gameStats.draws;
  }
  next();
});

const User = mongoose.model('User', userSchema);

module.exports = User; 