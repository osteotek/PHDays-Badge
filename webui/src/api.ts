// Badge HTTP API (same origin; `npm run dev` proxies it to a badge).

export interface Status {
  time: string | null;
  weather: {
    configured: boolean;
    valid: boolean;
    temperature?: number;
    code?: number;
    is_day?: boolean;
    high?: number;
    low?: number;
    age_s?: number;
  };
  timer: TimerState;
  screen: 'clock' | 'weather' | 'timer';
  screen_on: boolean;
  night: boolean;
  battery: number;
  battery_mv: number;
  app_version: string;
}

export interface TimerState {
  phase: 'idle' | 'focus' | 'break';
  remaining_s: number;
  total_s: number;
}

export interface Settings {
  show_clock: boolean;
  show_weather: boolean;
  transitions: boolean;
  power_save: boolean;
  screen_seconds: number;
  brightness: number;
  timezone: string;
  focus_minutes: number;
  break_minutes: number;
  alert_melody: string;
  night_mode: boolean;
  night_start: string; // "HH:MM"
  night_end: string;
  night_brightness: number; // 0 = LEDs off
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, init);
  const text = await response.text();
  if (!response.ok) throw new Error(text.trim() || `HTTP ${response.status}`);
  return (response.headers.get('content-type')?.includes('json') ? JSON.parse(text) : text) as T;
}

function postJson<T>(path: string, body: unknown): Promise<T> {
  return request<T>(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}

export const api = {
  status: () => request<Status>('/api/v1/status'),
  settings: () => request<Settings>('/api/v1/settings'),
  saveSettings: (changes: Partial<Settings>) => postJson<Settings>('/api/v1/settings', changes),
  timer: (action: 'start' | 'stop' | 'skip') => postJson<TimerState>('/api/v1/timer', { action }),
  melodies: () => request<unknown>('/api/v1/buzzer/melodies'),
  saveMelodies: (melodies: string[]) => postJson<string>('/api/v1/buzzer/melodies', { melodies }),
  play: (melody: string) => postJson<string>('/api/v1/buzzer/melody', { melody }),
  notify: (message: { text: string; color?: string; repeat?: number; sound?: boolean }) =>
    postJson<{ duration_ms: number }>('/api/v1/notify', message),
};
