import React from 'react';
import axios from 'axios';
import { API_URL } from '../utils/constants';

const pako = require('pako');

function uploadMelodyToBadge(composition) {
  console.log(composition);
  axios
    .post(`${API_URL}/buzzer/melody`, { melody: composition }, {})
    .then(reponse => {
      console.log(reponse);
    })
    .catch(error => {
      console.error(error);
    });
}

const melodiesList = [
  'Super Mario:d=4,o=5,b=100:16e6,16e6,32p,8e6,16c6,8e6,8g6,8p,8g,8p,8c6,16p,8g,16p,8e,16p,8a,8b,16a#,8a,16g.,16e6,16g6,8a6,16f6,8g6,8e6,16c6,16d6,8b,16p,8c6,16p,8g,16p,8e,16p,8a,8b,16a#,8a,16g.,16e6,16g6,8a6,16f6,8g6,8e6,16c6,16d6,8b,8p,16g6,16f#6,16f6,16d#6,16p,16e6,16p,16g#,16a,16c6,16p,16a,16c6,16d6,8p,16g6,16f#6,16f6,16d#6,16p,16e6,16p,16c7,16p,16c7,16c7,p,16g6,16f#6,16f6,16d#6,16p,16e6,16p,16g#,16a,16c6,16p,16a,16c6,16d6,8p,16d#6,8p,16d6,8p,16c6',
  'A-Team:d=8,o=5,b=125:4d#6,a#,2d#6,16p,g#,4a#,4d#.,p,16g,16a#,d#6,a#,f6,2d#6,16p,c#.6,16c6,16a#,g#.,2a#',
  'Mission Impossible:d=16,o=6,b=95:32d,32d#,32d,32d#,32d,32d#,32d,32d#,32d,32d,32d#,32e,32f,32f#,32g,g,8p,g,8p,a#,p,c7,p,g,8p,g,8p,f,p,f#,p,g,8p,g,8p,a#,p,c7,p,g,8p,g,8p,f,p,f#,p,a#,g,2d,32p,a#,g,2c#,32p,a#,g,2c,a#5,8c,2p,32p,a#5,g5,2f#,32p,a#5,g5,2f,32p,a#5,g5,2e,d#,8d',
  'The Simpsons:d=4,o=5,b=160:c.6,e6,f#6,8a6,g.6,e6,c6,8a,8f#,8f#,8f#,2g,8p,8p,8f#,8f#,8f#,8g,a#.,8c6,8c6,8c6,c6',
  'Indiana Jones:d=4,o=5,b=250:e,8p,8f,8g,8p,1c6,8p.,d,8p,8e,1f,p.,g,8p,8a,8b,8p,1f6,p,a,8p,8b,2c6,2d6,2e6,e,8p,8f,8g,8p,1c6,p,d6,8p,8e6,1f.6,g,8p,8g,e.6,8p,d6,8p,8g,e.6,8p,d6,8p,8g,f.6,8p,e6,8p,8d6,2c6',
  'James Bond:d=4,o=5,b=320:c,8d,8d,d,2d,c,c,c,c,8d#,8d#,2d#,d,d,d,c,8d,8d,d,2d,c,c,c,c,8d#,8d#,d#,2d#,d,c#,c,c6,1b.,g,f,1g.',
  'Star Wars:d=4,o=5,b=45:32p,32f#,32f#,32f#,8b.,8f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32e6,8c#.6,32f#,32f#,32f#,8b.,8f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32c#6,8b.6,16f#.6,32e6,32d#6,32e6,8c#6',
  'Flinstones:d=4,o=5,b=40:32p,16f6,16a#,16a#6,32g6,16f6,16a#.,16f6,32d#6,32d6,32d6,32d#6,32f6,16a#,16c6,d6,16f6,16a#.,16a#6,32g6,16f6,16a#.,32f6,32f6,32d#6,32d6,32d6,32d#6,32f6,16a#,16c6,a#,16a6,16d.6,16a#6,32a6,32a6,32g6,32f#6,32a6,8g6,16g6,16c.6,32a6,32a6,32g6,32g6,32f6,32e6,32g6,8f6,16f6,16a#.,16a#6,32g6,16f6,16a#.,16f6,32d#6,32d6,32d6,32d#6,32f6,16a#,16c.6,32d6,32d#6,32f6,16a#,16c.6,32d6,32d#6,32f6,16a#6,16c7,8a#.6'
];

const toDefaults = unparsedDefaults =>
  unparsedDefaults.split(',').reduce(
    (defaults, option) => {
      const [key, value] = option.split('=');
      switch (key) {
        case 'd':
          return { ...defaults, duration: value };
        case 'o':
          return { ...defaults, octave: value };
        case 'b':
          return { ...defaults, beat: value };
        default:
          return defaults;
      }
    },
    { duration: 4, octave: 6, beat: 63 }
  );

const toMelody = (melody, defaults) => {
  const notes = ['c', 'c#', 'd', 'd#', 'e', 'f', 'f#', 'g', 'g#', 'a', 'a#', 'b'];
  const middleC = 261.63;

  return melody.split(',').map(unparsedNote => {
    const { groups: parsed } = unparsedNote.match(/(?<duration>1|2|4|8|16|32|64)?(?<note>(?:[a-g]|p)#?){1}(?<dot>\.?)(?<octave>4|5|6|7)?/);
    const { duration, note, dot, octave, beat } = Object.keys(parsed).reduce(
      (xs, x) => (parsed[x] ? { ...xs, [x]: parsed[x] } : xs),
      defaults
    );

    return {
      duration: (240 / beat / duration) * (dot ? 1.5 : 1),
      frequency: note === 'p' ? 0 : middleC * 2 ** (octave - 4 + notes.indexOf(note) / 12)
    };
  });
};

const parse = rtttl => {
  const [_, unparsedDefaults, unparsedMelody] = rtttl.split(':', 3);
  return toMelody(unparsedMelody, toDefaults(unparsedDefaults));
};

const Buzzer = () => {
  const [isPlaying, setPlaying] = React.useState(false);
  const [melodies, setMelodies] = React.useState([]);
  const [composition, setComposition] = React.useState('');
  const [selectedComposition, setSelectedComposition] = React.useState('');

  React.useEffect(() => {
    axios
      .get(`${API_URL}/buzzer/melodyiesList`)
      .then(response => {
        console.log(response);
        const newMelodiesList = Object.keys(response.data.melodies).length > 0 ? response.data.melodies : melodiesList;
        setMelodies(newMelodiesList);
        setComposition(newMelodiesList[0]);
        setSelectedComposition(newMelodiesList[0]);
      })
      .catch(error => {
        console.log('someshit lalalal jajajaj');
        console.error(error);
        setMelodies(melodiesList);
        setComposition(melodiesList[0]);
        setSelectedComposition(melodiesList[0]);
      });
  }, []);

  React.useEffect(() => {
    if (!isPlaying) return;

    const audio = new AudioContext();
    const oscillator = audio.createOscillator();
    oscillator.type = 'square';
    oscillator.connect(audio.destination);
    oscillator.onended = () => setPlaying(false);

    let time = 0;
    oscillator.start();
    parse(composition).forEach(({ duration, frequency }) => {
      oscillator.frequency.setValueAtTime(frequency, time);
      time += duration;
    });
    oscillator.stop(time);

    return async () => {
      await audio.close();
    };
  }, [isPlaying]);

  function addNewMelody(newMelody) {
    let [melodyName, _, __] = newMelody.split(':', 3);

    const filteredMelodies = melodies.filter(el => {
      if (el.split(':', 3)[0] === melodyName) {
        return false;
      }
      return true;
    });
    filteredMelodies.push(newMelody);
    setMelodies(filteredMelodies);
    setSelectedComposition(newMelody);

    const prepared = pako.gzip(Buffer.from(JSON.stringify({ melodies: filteredMelodies })));
    axios
      .post(`${API_URL}/buzzer/melodyiesList`, prepared, {})
      .then(reponse => {
        console.log(reponse);
      })
      .catch(error => {
        console.error(error);
      });
  }

  return (
    <div className="buzzer">
      <div className="buzzer-load-drawing__container">
        <div className="left col-1-4">
          <button
            type="button"
            className={isPlaying ? 'pause' : 'play'}
            onClick={() => setPlaying(!isPlaying)}
            aria-label="Animation control"
          />
          <button type="button" className="import__button_2" onClick={() => addNewMelody(composition)}>
            Сохранить
          </button>
          <button type="button" className="import__button" onClick={() => uploadMelodyToBadge(composition)}>
            На Бейдж
          </button>
          <select
            className="buzzer_selector"
            onChange={e => {
              setComposition(e.target.value);
              setSelectedComposition(e.target.value);
            }}
            value={selectedComposition}
          >
            {melodies.map(melody => (
              <option value={melody}>{melody.split(':')[0]}</option>
            ))}
          </select>
        </div>
        <div className="right col-3-4">
          <div className="load-drawing__drawing">
            <textarea
              className="load-drawing__import"
              value={composition}
              disabled={isPlaying}
              onChange={e => setComposition(e.target.value)}
            />
          </div>
        </div>
      </div>
    </div>
  );
};

export default Buzzer;
