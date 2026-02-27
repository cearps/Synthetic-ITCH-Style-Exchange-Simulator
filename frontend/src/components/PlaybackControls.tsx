interface Props {
  speed: number;
  paused: boolean;
  done: boolean;
  onSetSpeed: (speed: number) => void;
  onPause: () => void;
  onResume: () => void;
  onStop: () => void;
}

const SPEEDS = [10, 50, 100, 200, 500, 1000, 2000, 5000];

export default function PlaybackControls({ speed, paused, done, onSetSpeed, onPause, onResume, onStop }: Props) {
  if (done) return null;

  return (
    <div className="playback-controls">
      <button
        className="playback-btn playback-pause"
        onClick={paused ? onResume : onPause}
        title={paused ? "Resume" : "Pause"}
      >
        {paused ? "▶" : "⏸"}
      </button>

      <div className="playback-speed-group">
        {SPEEDS.map((s) => (
          <button
            key={s}
            className={`playback-speed-btn ${s === speed ? "active" : ""}`}
            onClick={() => onSetSpeed(s)}
          >
            {s >= 1000 ? `${s / 1000}k` : s}x
          </button>
        ))}
      </div>

      <button className="btn-stop" onClick={onStop}>Stop</button>
    </div>
  );
}
