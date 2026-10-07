import { describe, expect, it } from 'vitest';
import { BADGE_MAX_NOTES_TEXT, parseRtttl } from '../src/rtttl';

describe('parseRtttl', () => {
  it('applies defaults, durations, dots and octaves', () => {
    const { name, notes } = parseRtttl('Beep:d=8,o=5,b=120:c,4e6,g.,p');
    expect(name).toBe('Beep');
    expect(notes).toHaveLength(4);
    expect(notes[0].frequency).toBeCloseTo(261.63 * 2); // c5; octave 4 is middle C, as on the badge
    expect(notes[0].seconds).toBeCloseTo(0.25); // eighth note at 120 bpm
    expect(notes[1].frequency).toBeCloseTo(329.63 * 4, 0); // e6
    expect(notes[1].seconds).toBeCloseTo(0.5);
    expect(notes[2].seconds).toBeCloseTo(0.375); // dotted
    expect(notes[3].frequency).toBe(0); // pause
  });

  it('uses the badge defaults without a defaults section', () => {
    const { notes } = parseRtttl('X::c');
    expect(notes[0].seconds).toBeCloseTo(0.5); // quarter note at 120 bpm
    expect(notes[0].frequency).toBeCloseTo(261.63 * 4); // octave 6
  });

  it('accepts the dot before or after the octave and sharps', () => {
    expect(parseRtttl('X:d=4:8g.6').notes[0].seconds).toBeCloseTo(0.375);
    expect(parseRtttl('X:d=4:8g6.').notes[0].seconds).toBeCloseTo(0.375);
    expect(parseRtttl('X:o=4:c#').notes[0].frequency).toBeCloseTo(277.18, 1);
  });

  it('rejects what the badge would refuse', () => {
    expect(() => parseRtttl('no colons')).toThrow(/name:defaults:notes/);
    expect(() => parseRtttl(':d=4:c')).toThrow(/name/);
    expect(() => parseRtttl('X:b=0:c')).toThrow(/tempo/);
    expect(() => parseRtttl('X:d=0:c')).toThrow(/duration/);
    expect(() => parseRtttl('X::0c')).toThrow(/duration/);
    expect(() => parseRtttl('X:o=9:c')).toThrow(/octave/);
    expect(() => parseRtttl('X::c9')).toThrow(/octave/);
    expect(() => parseRtttl('X:q=1:c')).toThrow(/Unknown default/);
    expect(() => parseRtttl('X::h')).toThrow(/not a note/);
    expect(() => parseRtttl(`X::${'c,'.repeat(BADGE_MAX_NOTES_TEXT)}c`)).toThrow(/longer/);
  });

  it("parses the badge's default melodies", () => {
    const defaults = [
      "Alert:d=8,o=6,b=180:c,e,g,c7",
      "A-Team:d=8,o=5,b=125:4d#6,a#,2d#6,16p,g#,4a#,4d#.,p,16g,16a#,d#6,a#,f6,2d#6,16p,c#.6,16c6,16a#,g#.,2a#",
      "The Simpsons:d=4,o=5,b=160:c.6,e6,f#6,8a6,g.6,e6,c6,8a,8f#,8f#,8f#,2g,8p,8p,8f#,8f#,8f#,8g,a#.,8c6,8c6,8c6,c6",
      "Indiana Jones:d=4,o=5,b=250:e,8p,8f,8g,8p,1c6,8p.,d,8p,8e,1f,p.,g,8p,8a,8b,8p,1f6,p,a,8p,8b,2c6,2d6,2e6,e,8p,8f,8g,8p,1c6,p,d6,8p,8e6,1f.6",
      "James Bond:d=4,o=5,b=320:c,8d,8d,d,2d,c,c,c,c,8d#,8d#,2d#,d,d,d,c,8d,8d,d,2d,c,c,c,c,8d#,8d#,d#,2d#,d,c#,c,c6,1b.,g,f,1g.",
    ];
    for (const melody of defaults) expect(() => parseRtttl(melody)).not.toThrow();
  });
});
