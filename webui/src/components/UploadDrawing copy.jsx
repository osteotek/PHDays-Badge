import React from 'react';
import renderFrames from '../utils/canvasGIF';

const UploadDrawing = props => {
  const DOWNLOAD_MESSAGE = 'Downloading...';
  const download = type => {
    const { frames, activeFrame, columns, rows, cellSize, duration } = props;
    const upload = true;
    props.actions.sendNotification(DOWNLOAD_MESSAGE);
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
    <button
      type="button"
      className="upload-btn"
      onClick={() => {
        download(props.downloadType);
      }}
    >
      Upload
    </button>
  );
};

export default UploadDrawing;
