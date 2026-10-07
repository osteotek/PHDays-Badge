// The badge uses POSIX TZ strings, whose offsets count west of UTC:
// UTC+3 is "<+03>-3". This covers fixed offsets; zones with daylight saving
// need a full rule such as "CET-1CEST,M3.5.0,M10.5.0/3".
export function posixFromOffset(minutesEastOfUtc: number): string {
  const sign = minutesEastOfUtc >= 0 ? '+' : '-';
  const total = Math.abs(minutesEastOfUtc);
  const hours = Math.floor(total / 60);
  const minutes = total % 60;
  const pad = (n: number) => String(n).padStart(2, '0');
  const name = `<${sign}${pad(hours)}${minutes ? pad(minutes) : ''}>`;
  const offset = `${minutesEastOfUtc > 0 ? '-' : ''}${hours}${minutes ? `:${pad(minutes)}` : ''}`;
  return name + offset;
}

export function browserTimezone(now = new Date()): string {
  return posixFromOffset(-now.getTimezoneOffset());
}
