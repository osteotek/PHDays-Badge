// Saved melodies are RTTTL strings, identified by their name (before the first ":").
// The badge serves its default list until melodies are saved.

export function melodyName(melody: string): string {
  return melody.split(':', 1)[0].trim();
}

function sameName(a: string, b: string): boolean {
  return melodyName(a).toLowerCase() === melodyName(b).toLowerCase();
}

// Replaces the melody with the same name, or appends it.
export function upsertMelody(list: readonly string[], melody: string): string[] {
  const index = list.findIndex((item) => sameName(item, melody));
  return index < 0 ? [...list, melody] : list.map((item, i) => (i === index ? melody : item));
}

export function removeMelody(list: readonly string[], name: string): string[] {
  return list.filter((item) => melodyName(item).toLowerCase() !== name.toLowerCase());
}

// The badge stores {"melodies": [...]}; anything else counts as "nothing saved".
export function melodiesFromFile(data: unknown): string[] {
  const melodies = (data as { melodies?: unknown } | null)?.melodies;
  return Array.isArray(melodies) ? melodies.filter((m): m is string => typeof m === 'string') : [];
}
