import GIFEncoder from 'gif-encoder';
import blobStream from 'blob-stream';
import { saveAs } from 'file-saver';
import axios from 'axios';
import randomString from './random';
import { API_URL } from './constants';

axios.defaults.headers.post['Access-Control-Allow-Origin'] = '*';

function fillCanvasWithFrame(canvas, frameInfo) {
  const { frame, cols, cellSize, frameHeight, frameIdx } = frameInfo;
  const ctx = canvas;
  frame.get('grid').forEach((fillStyle, pixelIdx) => {
    if (!fillStyle) {
      return;
    }
    ctx.fillStyle = fillStyle;

    const col = pixelIdx % cols;
    const row = Math.floor(pixelIdx / cols);
    ctx.fillRect(col * cellSize, row * cellSize + frameHeight * frameIdx, cellSize, cellSize);
  });
  return ctx;
}

function renderImageToCanvas(type, canvasInfo, currentFrameInfo, frames) {
  const { canvas, canvasHeight, canvasWidth } = canvasInfo;
  const { frame, frameHeight, frameWidth, cellSize } = currentFrameInfo;
  const cols = Math.floor(frameWidth / cellSize);
  let ctx = canvas.getContext('2d');
  ctx.canvas.width = canvasWidth;
  ctx.canvas.height = canvasHeight;
  switch (type) {
    case 'spritesheet':
      frames.forEach((currentFrame, frameIdx) => {
        ctx = fillCanvasWithFrame(ctx, {
          frame: currentFrame,
          cols,
          cellSize,
          frameHeight,
          frameIdx
        });
      });
      break;
    default:
      ctx = fillCanvasWithFrame(ctx, {
        frame,
        cols,
        cellSize,
        frameHeight,
        frameIdx: 0
      });
      break;
  }
  return ctx.getImageData(0, 0, canvasWidth, canvasHeight).data;
}

const saveCanvasToDisk = (blob, fileExtension) => {
  saveAs(blob, `${randomString()}.${fileExtension}`);
};

function uploadToBadge(pixelData) {
  const rgbArray = [];
  pixelData.forEach(el => {
    const { delay, frame } = el;
    const locRgbArray = [];
    for (let i = 0; i < frame.length; i += 4) {
      locRgbArray.push(frame[i], frame[i + 1], frame[i + 2]);
    }
    rgbArray.push({
      delay: Math.floor(delay),
      frame: locRgbArray
    });
  });

  const reqBody = { frames: rgbArray };
  axios
    .post(`${API_URL}/led/picture`, reqBody, {})
    .then(reponse => {
      console.log(reponse);
    })
    .catch(error => {
      console.error(error);
    });
}

function copyToClip(pixelData) {
  let imgStructSrting = '';
  // console.log(pixelData);

  imgStructSrting += '(Image){{';

  pixelData.forEach(el => {
    const { delay, frame } = el;
    imgStructSrting += `(Frame){.pixels ={`;
    for (let i = 0; i < frame.length; i += 4) {
      imgStructSrting += `{${frame[i]},${frame[i + 1]},${frame[i + 2]}},`;
    }
    imgStructSrting += `}, .duration = ${Math.floor(delay)}},`;
  });

  imgStructSrting += `}, .shiftMode = 0, .framesCount = ${pixelData.length}};`;

  console.log(imgStructSrting);
  // navigator.clipboard.writeText(imgStructSrting);
  alert(imgStructSrting);
  if ('clipboard' in navigator) {
    navigator.clipboard.writeText(imgStructSrting);
  } else {
    document.execCommand('copy', true, imgStructSrting);
  }
}

function renderFrames(settings) {
  const { type, frames, duration, activeFrame, rows, columns, cellSize, upload } = settings;

  const durationInMillisecond = duration * 1000;
  const frameWidth = columns * cellSize;
  const frameHeight = rows * cellSize;
  const canvasWidth = frameWidth;
  const canvasHeight = type === 'spritesheet' ? frameHeight * frames.size : frameHeight;

  const canvas = document.createElement('canvas');
  const gif = new GIFEncoder(canvasWidth, canvasHeight);
  gif.pipe(blobStream()).on('finish', function () {
    saveCanvasToDisk(this.toBlob(), 'gif');
  });

  gif.setRepeat(0); // loop indefinitely
  gif.setDispose(3); // restore to previous
  gif.writeHeader();

  const framesRGB = [];

  switch (type) {
    case 'single':
    case 'framesArray':
      {
        let previousInterval = 0;
        frames.forEach((frame, idx, framesArray) => {
          const isLastFrame = idx === framesArray.length - 1;
          const currentInterval = isLastFrame ? 100 : frames.get(idx).get('interval');
          const diff = currentInterval - previousInterval;
          const frameDelay = diff * 0.01 * durationInMillisecond;
          previousInterval = currentInterval;

          framesRGB.push({
            delay: frameDelay,
            frame: renderImageToCanvas(
              type,
              {
                canvas,
                canvasHeight,
                canvasWidth
              },
              {
                frame,
                frameHeight,
                frameWidth,
                cellSize
              }
            )
          });
        });
        if (upload) uploadToBadge(framesRGB);
        else copyToClip(framesRGB);
      }
      break;
    case 'spritesheet':
      {
        const pixels = renderImageToCanvas(
          type,
          {
            canvas,
            canvasHeight,
            canvasWidth
          },
          {
            frame: activeFrame,
            frameHeight,
            frameWidth,
            cellSize
          },
          frames
        );

        if (upload) {
          uploadToBadge(pixels);
        } else {
          canvas.toBlob(function (blob) {
            saveCanvasToDisk(blob, 'png');
          });
        }
      }
      break;
    default: {
      let previousInterval = 0;
      frames.forEach((frame, idx, framesArray) => {
        const isLastFrame = idx === framesArray.length - 1;
        const currentInterval = isLastFrame ? 100 : frames.get(idx).get('interval');
        const diff = currentInterval - previousInterval;
        const delay = diff * 0.01 * durationInMillisecond;

        gif.setDelay(delay);
        previousInterval = currentInterval;

        gif.addFrame(
          renderImageToCanvas(
            type,
            {
              canvas,
              canvasHeight,
              canvasWidth
            },
            {
              frame,
              frameHeight,
              frameWidth,
              cellSize
            }
          )
        );
      });
      gif.finish();
    }
  }
}

export default renderFrames;
