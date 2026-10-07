// RTTTL ("name:d=4,o=6,b=120:c,8e,g.") parsing with the badge's limits, used to
// validate melodies before saving and to preview them in the browser.

export interface Note {
  frequency: number; // Hz, 0 for a pause
  seconds: number;
}

export interface Melody {
  name: string;
  notes: Note[];
}

// The badge plays the first 100 notes and keeps at most 1023 characters of notes.
export const BADGE_MAX_NOTES = 100;
export const BADGE_MAX_NOTES_TEXT = 1023;

const NOTE_NAMES = ['c', 'c#', 'd', 'd#', 'e', 'f', 'f#', 'g', 'g#', 'a', 'a#', 'b'];
const MIDDLE_C = 261.63;

function checkDuration(value: number, where: string): number {
  if (!(value >= 1 && value <= 32)) throw new Error(`${where}: duration must be 1-32`);
  return value;
}

function checkOctave(value: number, where: string): number {
  if (!(value >= 0 && value <= 8)) throw new Error(`${where}: octave must be 0-8`);
  return value;
}

export function parseRtttl(text: string): Melody {
  const parts = text.split(':');
  if (parts.length !== 3) throw new Error('Use name:defaults:notes, e.g. Beep:d=8,o=6,b=180:c,e,g');
  const [name, defaultsText, notesText] = parts.map((part) => part.trim());
  if (!name) throw new Error('The melody needs a name before the first ":"');
  if (notesText.length > BADGE_MAX_NOTES_TEXT) throw new Error(`Notes are longer than ${BADGE_MAX_NOTES_TEXT} characters`);

  let duration = 4;
  let octave = 6;
  let bpm = 120;
  for (const option of defaultsText.split(',').map((o) => o.trim()).filter(Boolean)) {
    const match = /^([dob])=(\d+)$/i.exec(option);
    if (!match) throw new Error(`Unknown default "${option}" (use d=, o= or b=)`);
    const value = Number(match[2]);
    const key = match[1].toLowerCase();
    if (key === 'd') duration = checkDuration(value, 'Defaults');
    else if (key === 'o') octave = checkOctave(value, 'Defaults');
    else if (value < 1) throw new Error('Defaults: tempo (b=) must be at least 1');
    else bpm = value;
  }

  const notes = notesText.split(',').map((token, index) => {
    const where = `Note ${index + 1} "${token.trim()}"`;
    const match = /^(\d{1,2})?([a-gp])(#)?(\.)?(\d)?(\.)?$/i.exec(token.trim());
    if (!match) throw new Error(`${where}: not a note`);
    const [, length, letter, sharp, dotBefore, scale, dotAfter] = match;
    const noteDuration = length ? checkDuration(Number(length), where) : duration;
    const noteOctave = scale ? checkOctave(Number(scale), where) : octave;
    const pitch = NOTE_NAMES.indexOf(letter.toLowerCase() + (sharp ?? ''));
    return {
      frequency: letter.toLowerCase() === 'p' ? 0 : MIDDLE_C * 2 ** (noteOctave - 4 + pitch / 12),
      seconds: (60 / bpm) * (4 / noteDuration) * (dotBefore || dotAfter ? 1.5 : 1),
    };
  });
  return { name, notes };
}
