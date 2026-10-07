import './style.css';
import { api, type Settings, type Status } from './api';
import { describeWeather, formatAge, formatCountdown } from './format';
import { melodiesFromFile, melodyName, removeMelody, upsertMelody } from './melodies';
import { BADGE_MAX_NOTES, parseRtttl } from './rtttl';
import { browserTimezone } from './timezone';

const $ = <T extends HTMLElement>(selector: string) => document.querySelector<T>(selector)!;
const form = $<HTMLFormElement>('#settings');
const field = <T extends HTMLElement>(name: string) => form.elements.namedItem(name) as unknown as T;

function say(target: HTMLElement, text: string, error = false) {
  target.textContent = text;
  target.classList.toggle('error', error);
}

const describe = (error: unknown) => (error instanceof Error ? error.message : String(error));

// --- status -------------------------------------------------------------------

function showStatus(status: Status) {
  const screen = `${status.screen}${status.screen_on ? '' : ' (screen off)'}${status.night ? ' (night mode)' : ''}`;
  $('#summary').textContent = `Firmware ${status.app_version} · battery ${status.battery}% · showing ${screen}`;
  $('#now-time').textContent = status.time?.slice(0, 5) ?? '--:--';
  const weather = status.weather;
  if (weather.valid && weather.code !== undefined && weather.temperature !== undefined) {
    const { icon, text } = describeWeather(weather.code, weather.is_day ?? true);
    $('#now-weather').textContent = `${icon} ${Math.round(weather.temperature)}°C`;
    const range = weather.high !== undefined && weather.low !== undefined ? `, today ${Math.round(weather.low)}–${Math.round(weather.high)}°C` : '';
    $('#now-weather-note').textContent = `${text}${range}, updated ${formatAge(weather.age_s ?? 0)}`;
  } else {
    $('#now-weather').textContent = '–';
    $('#now-weather-note').textContent = weather.configured ? 'waiting for data' : 'no location set';
  }
  $('#weather-hint').textContent = weather.configured ? '' : '(add [weather] latitude/longitude to wifi_secrets.ini)';
  const timer = status.timer;
  $('#timer-state').textContent = timer.phase === 'idle' ? 'Idle' : `${timer.phase === 'focus' ? 'Focus' : 'Break'} ${formatCountdown(timer.remaining_s)}`;
  $<HTMLButtonElement>('#timer-skip').disabled = timer.phase === 'idle';
  $<HTMLButtonElement>('#timer-stop').disabled = timer.phase === 'idle';
}

async function refreshStatus() {
  if (document.visibilityState === 'visible') {
    try {
      showStatus(await api.status());
    } catch {
      $('#summary').textContent = 'Badge unreachable, retrying…';
    }
  }
  setTimeout(refreshStatus, 1000);
}

for (const action of ['start', 'skip', 'stop'] as const) {
  $(`#timer-${action}`).addEventListener('click', async () => {
    try {
      await api.timer(action);
      showStatus(await api.status());
    } catch (error) {
      $('#summary').textContent = describe(error);
    }
  });
}

// --- settings -------------------------------------------------------------------

const settingsMessage = $('#settings-message');

function fillSettings(settings: Settings) {
  field<HTMLInputElement>('show_clock').checked = settings.show_clock;
  field<HTMLInputElement>('show_weather').checked = settings.show_weather;
  field<HTMLInputElement>('transitions').checked = settings.transitions;
  field<HTMLInputElement>('screen_seconds').value = String(settings.screen_seconds);
  field<HTMLInputElement>('brightness').value = String(settings.brightness);
  field<HTMLInputElement>('timezone').value = settings.timezone;
  field<HTMLInputElement>('focus_minutes').value = String(settings.focus_minutes);
  field<HTMLInputElement>('break_minutes').value = String(settings.break_minutes);
  field<HTMLInputElement>('night_mode').checked = settings.night_mode;
  field<HTMLInputElement>('night_start').value = settings.night_start;
  field<HTMLInputElement>('night_end').value = settings.night_end;
  field<HTMLInputElement>('night_brightness').value = String(settings.night_brightness);
  showNightBrightness();
  alertMelody = settings.alert_melody;
  renderMelodyOptions();
}

function readSettings(): Partial<Settings> {
  return {
    show_clock: field<HTMLInputElement>('show_clock').checked,
    show_weather: field<HTMLInputElement>('show_weather').checked,
    transitions: field<HTMLInputElement>('transitions').checked,
    screen_seconds: Number(field<HTMLInputElement>('screen_seconds').value),
    brightness: Number(field<HTMLInputElement>('brightness').value),
    timezone: field<HTMLInputElement>('timezone').value.trim(),
    focus_minutes: Number(field<HTMLInputElement>('focus_minutes').value),
    break_minutes: Number(field<HTMLInputElement>('break_minutes').value),
    alert_melody: field<HTMLSelectElement>('alert_melody').value,
    night_mode: field<HTMLInputElement>('night_mode').checked,
    night_start: field<HTMLInputElement>('night_start').value,
    night_end: field<HTMLInputElement>('night_end').value,
    night_brightness: Number(field<HTMLInputElement>('night_brightness').value),
  };
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  try {
    fillSettings(await api.saveSettings(readSettings()));
    say(settingsMessage, 'Saved');
  } catch (error) {
    say(settingsMessage, describe(error), true);
  }
});

// Brightness applies immediately while dragging.
let brightnessTimer: ReturnType<typeof setTimeout> | undefined;
field<HTMLInputElement>('brightness').addEventListener('input', (event) => {
  clearTimeout(brightnessTimer);
  const brightness = Number((event.target as HTMLInputElement).value);
  brightnessTimer = setTimeout(() => api.saveSettings({ brightness }).catch((error) => say(settingsMessage, describe(error), true)), 250);
});

function showNightBrightness() {
  const value = Number(field<HTMLInputElement>('night_brightness').value);
  $('#night-brightness-value').textContent = value === 0 ? '(off)' : `(${value})`;
}
field<HTMLInputElement>('night_brightness').addEventListener('input', showNightBrightness);

$('#use-browser-tz').addEventListener('click', () => {
  field<HTMLInputElement>('timezone').value = browserTimezone();
  say(settingsMessage, 'Fixed offset from this device; save to apply. Use a full POSIX rule for daylight saving.');
});

// --- message ----------------------------------------------------------------------

const messageForm = $<HTMLFormElement>('#message');
messageForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const input = (name: string) => messageForm.elements.namedItem(name) as HTMLInputElement;
  try {
    const { duration_ms } = await api.notify({
      text: input('text').value,
      color: input('color').value,
      repeat: Number(input('repeat').value),
      sound: input('sound').checked,
    });
    say($('#message-status'), `Showing for ${Math.round(duration_ms / 1000)} s`);
  } catch (error) {
    say($('#message-status'), describe(error), true);
  }
});

// --- melodies -------------------------------------------------------------------

let melodies: string[] = [];
let alertMelody = '';
const list = $<HTMLSelectElement>('#melody-list');
const text = $<HTMLTextAreaElement>('#melody-text');
const melodyMessage = $('#melody-message');

function options(select: HTMLSelectElement, values: string[], selected: string) {
  select.replaceChildren(...values.map((value) => new Option(melodyName(value), value, false, value === selected)));
}

function renderMelodyOptions() {
  options(list, melodies, text.value);
  // The alert can be any saved melody; keep the current one even if unsaved.
  const alertChoices = melodies.includes(alertMelody) || !alertMelody ? melodies : [alertMelody, ...melodies];
  options(field<HTMLSelectElement>('alert_melody'), alertChoices, alertMelody);
}

function checkMelody(): boolean {
  try {
    const melody = parseRtttl(text.value);
    $('#melody-note').textContent =
      melody.notes.length > BADGE_MAX_NOTES ? `${melody.notes.length} notes; the badge plays the first ${BADGE_MAX_NOTES}.` : `${melody.notes.length} notes`;
    return true;
  } catch (error) {
    $('#melody-note').textContent = describe(error);
    return false;
  }
}

function selectMelody(melody: string) {
  text.value = melody;
  list.value = melody;
  checkMelody();
}

list.addEventListener('change', () => selectMelody(list.value));
text.addEventListener('input', checkMelody);

async function saveMelodies(next: string[], done: string) {
  try {
    await api.saveMelodies(next);
    melodies = next;
    renderMelodyOptions();
    say(melodyMessage, done);
  } catch (error) {
    say(melodyMessage, describe(error), true);
  }
}

$('#melody-save').addEventListener('click', () => {
  if (checkMelody()) saveMelodies(upsertMelody(melodies, text.value.trim()), `Saved "${melodyName(text.value)}"`);
  else say(melodyMessage, 'Fix the melody first', true);
});

$('#melody-delete').addEventListener('click', () => {
  const name = melodyName(text.value);
  saveMelodies(removeMelody(melodies, name), `Deleted "${name}"`).then(() => selectMelody(melodies[0] ?? ''));
});

$('#melody-play').addEventListener('click', async () => {
  try {
    await api.play(text.value.trim());
    say(melodyMessage, 'Playing on the badge');
  } catch (error) {
    say(melodyMessage, describe(error), true);
  }
});

// Browser preview with a square wave, like the badge's piezo buzzer.
let preview: AudioContext | undefined;
$('#melody-preview').addEventListener('click', async () => {
  const button = $<HTMLButtonElement>('#melody-preview');
  if (preview) {
    await preview.close();
    preview = undefined;
    button.textContent = 'Preview here';
    return;
  }
  let notes;
  try {
    notes = parseRtttl(text.value).notes;
  } catch (error) {
    say(melodyMessage, describe(error), true);
    return;
  }
  const audio = (preview = new AudioContext());
  const oscillator = audio.createOscillator();
  const gain = audio.createGain();
  oscillator.type = 'square';
  gain.gain.value = 0.1;
  oscillator.connect(gain).connect(audio.destination);
  let time = audio.currentTime;
  for (const note of notes) {
    oscillator.frequency.setValueAtTime(note.frequency, time);
    gain.gain.setValueAtTime(note.frequency ? 0.1 : 0, time);
    time += note.seconds;
  }
  oscillator.onended = () => {
    if (preview === audio) {
      audio.close();
      preview = undefined;
      button.textContent = 'Preview here';
    }
  };
  oscillator.start();
  oscillator.stop(time);
  button.textContent = 'Stop preview';
});

// --- start ----------------------------------------------------------------------

async function start() {
  try {
    melodies = melodiesFromFile(await api.melodies());
  } catch (error) {
    say(melodyMessage, `Could not load melodies: ${describe(error)}`, true);
  }
  selectMelody(melodies[0] ?? '');
  try {
    fillSettings(await api.settings());
  } catch (error) {
    say(settingsMessage, `Could not load settings: ${describe(error)}`, true);
  }
  refreshStatus();
}

start();
