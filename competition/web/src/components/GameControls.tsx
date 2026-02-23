import React from 'react';
import type { GameState, GameResult } from '../lib/types';

interface GameControlsProps {
  gameState: GameState;
  onPlay: () => void;
  onPause: () => void;
  onStep: () => void;
  onReset: () => void;
  moveDelay: number;
  onMoveDelayChange: (ms: number) => void;
}

const resultLabels: Record<NonNullable<GameResult>, string> = {
  'white-checkmate': 'White wins by checkmate!',
  'black-checkmate': 'Black wins by checkmate!',
  'stalemate': 'Draw by stalemate',
  'draw-repetition': 'Draw by threefold repetition',
  'draw-insufficient': 'Draw by insufficient material',
  'draw-50-move': 'Draw by 50-move rule',
  'white-forfeit-invalid': 'Black wins — White made an invalid move',
  'black-forfeit-invalid': 'White wins — Black made an invalid move',
  'white-forfeit-timeout': 'Black wins — White timed out',
  'black-forfeit-timeout': 'White wins — Black timed out',
};

export const GameControls: React.FC<GameControlsProps> = ({
  gameState,
  onPlay,
  onPause,
  onStep,
  onReset,
  moveDelay,
  onMoveDelayChange,
}) => {
  const isRunning = gameState.status === 'running';
  const isFinished = gameState.status === 'finished';
  const isIdle = gameState.status === 'idle';
  const isWaitingHuman = gameState.status === 'waiting-human';

  const statusLabel = isWaitingHuman ? 'YOUR TURN' : gameState.status.toUpperCase();

  return (
    <div className="game-controls">
      <div className="controls-buttons">
        {!isRunning && !isWaitingHuman ? (
          <button onClick={onPlay} disabled={isFinished || !gameState.whitePlayer}>
            ▶ Play
          </button>
        ) : (
          <button onClick={onPause}>⏸ Pause</button>
        )}
        <button onClick={onStep} disabled={isRunning || isWaitingHuman || isFinished || !gameState.whitePlayer}>
          ⏭ Step
        </button>
        <button onClick={onReset} disabled={isIdle}>
          ↺ Reset
        </button>
      </div>

      <div className="controls-speed">
        <label>
          Move delay: {moveDelay}ms
          <input
            type="range"
            min={0}
            max={2000}
            step={100}
            value={moveDelay}
            onChange={(e) => onMoveDelayChange(Number(e.target.value))}
          />
        </label>
      </div>

      <div className="controls-status">
        <div className="status-row">
          <span className="status-label">Status:</span>
          <span className={`status-value status-${isWaitingHuman ? 'waiting-human' : gameState.status}`}>
            {statusLabel}
          </span>
        </div>
        <div className="status-row">
          <span className="status-label">Turn:</span>
          <span>{gameState.currentTurn === 'w' ? 'White' : 'Black'}</span>
        </div>
        <div className="status-row">
          <span className="status-label">Moves:</span>
          <span>{gameState.moves.length}</span>
        </div>
        <div className="status-row">
          <span className="status-label">Time limit:</span>
          <span>{(gameState.timeLimitMs / 1000).toFixed(1)}s</span>
        </div>
        {gameState.lastMoveTimeMs > 0 && (
          <div className="status-row">
            <span className="status-label">Last move:</span>
            <span>{gameState.lastMoveTimeMs}ms</span>
          </div>
        )}
      </div>

      {isFinished && gameState.result && (
        <div className="game-result">
          {resultLabels[gameState.result]}
        </div>
      )}
    </div>
  );
};
