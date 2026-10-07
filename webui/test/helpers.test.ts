import { describe, expect, it } from 'vitest';
import { describeWeather, formatAge, formatCountdown } from '../src/format';
import { melodiesFromFile, melodyName, removeMelody, upsertMelody } from '../src/melodies';
import { posixFromOffset } from '../src/timezone';

describe('posixFromOffset', () => {
  it('inverts the sign as POSIX TZ requires', () => {
    expect(posixFromOffset(180)).toBe('<+03>-3'); // Moscow
    expect(posixFromOffset(-300)).toBe('<-05>5');
    expect(posixFromOffset(330)).toBe('<+0530>-5:30');
    expect(posixFromOffset(0)).toBe('<+00>0');
  });
});

describe('melodies', () => {
  const list = ['A:d=4:c', 'B:d=4:e'];

  it('names melodies by the text before the first colon', () => {
    expect(melodyName(' Alert :d=8:c')).toBe('Alert');
  });

  it('replaces a melody with the same name, case-insensitively', () => {
    expect(upsertMelody(list, 'b:d=8:g')).toEqual(['A:d=4:c', 'b:d=8:g']);
    expect(upsertMelody(list, 'C:d=4:a')).toEqual([...list, 'C:d=4:a']);
  });

  it('removes by name', () => {
    expect(removeMelody(list, 'a')).toEqual(['B:d=4:e']);
  });

  it('reads only well-formed melody files', () => {
    expect(melodiesFromFile({ melodies: ['A:d=4:c', 5] })).toEqual(['A:d=4:c']);
    expect(melodiesFromFile({})).toEqual([]);
    expect(melodiesFromFile(null)).toEqual([]);
  });
});

describe('format', () => {
  it('formats countdowns', () => {
    expect(formatCountdown(1500)).toBe('25:00');
    expect(formatCountdown(65)).toBe('1:05');
    expect(formatCountdown(-3)).toBe('0:00');
  });

  it('describes WMO weather codes', () => {
    expect(describeWeather(0, false).icon).toBe('🌙');
    expect(describeWeather(63, true).text).toBe('Rain');
    expect(describeWeather(75, true).text).toBe('Snow');
    expect(describeWeather(96, true).text).toBe('Thunderstorm');
  });

  it('describes data age', () => {
    expect(formatAge(30)).toBe('just now');
    expect(formatAge(600)).toBe('10 min ago');
    expect(formatAge(7200)).toBe('2 h ago');
  });
});
