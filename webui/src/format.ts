export function formatCountdown(totalSeconds: number): string {
  const seconds = Math.max(0, Math.round(totalSeconds));
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}

// WMO weather interpretation codes, as used by Open-Meteo.
export function describeWeather(code: number, isDay: boolean): { icon: string; text: string } {
  if (code === 0) return isDay ? { icon: '☀️', text: 'Clear' } : { icon: '🌙', text: 'Clear' };
  if (code === 1 || code === 2) return { icon: isDay ? '🌤️' : '☁️', text: code === 1 ? 'Mainly clear' : 'Partly cloudy' };
  if (code === 3) return { icon: '☁️', text: 'Overcast' };
  if (code === 45 || code === 48) return { icon: '🌫️', text: 'Fog' };
  if (code >= 51 && code <= 57) return { icon: '🌦️', text: 'Drizzle' };
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return { icon: '🌧️', text: 'Rain' };
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return { icon: '🌨️', text: 'Snow' };
  if (code >= 95 && code <= 99) return { icon: '⛈️', text: 'Thunderstorm' };
  return { icon: '☁️', text: 'Cloudy' };
}

export function formatAge(seconds: number): string {
  if (seconds < 90) return 'just now';
  if (seconds < 3600) return `${Math.round(seconds / 60)} min ago`;
  return `${Math.round(seconds / 3600)} h ago`;
}
