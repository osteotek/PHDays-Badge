import React from 'react';
import { connect } from 'react-redux';
import renderFrames from '../utils/canvasGIF';

const UploadDrawing = props => {
  const uploadSprite = type => {
    console.log(props);
    const { frames, activeFrame, columns, rows, cellSize, duration } = props;
    const upload = true;
    renderFrames({
      type,
      frames,
      activeFrame,
      columns,
      rows,
      cellSize,
      duration,
      upload
    });
  };

  const uploadSpriteToClip = type => {
    console.log(props);
    const { frames, activeFrame, columns, rows, cellSize, duration } = props;
    const upload = false;
    renderFrames({
      type,
      frames,
      activeFrame,
      columns,
      rows,
      cellSize,
      duration,
      upload
    });
  };

  return (
    <div>
      <div className="upload-project">
        <button
          type="button"
          onClick={() => {
            uploadSprite('framesArray');
          }}
        >
          НА БЕЙДЖ
        </button>
      </div>
      <div className="upload-project">
        <button
          type="button"
          onClick={() => {
            uploadSpriteToClip('framesArray');
          }}
        >
          В БУФФЕР
        </button>
      </div>
    </div>
  );
};

const mapStateToProps = state => {
  const frames = state.present.get('frames');
  const dur = state.present.get('duration');
  return {
    frames: frames.get('list'),
    columns: frames.get('columns'),
    rows: frames.get('rows'),
    cellSize: state.present.get('cellSize'),
    paletteGridData: state.present.getIn(['palette', 'grid']),
    duration: dur
  };
};

const UploadDrawingContainer = connect(mapStateToProps)(UploadDrawing);
export default UploadDrawingContainer;
